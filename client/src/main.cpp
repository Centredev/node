#include <peer.hpp>
#include <cmdlineargs.hpp>

#include "stdafx.h"

#include <iostream>
#include <filesystem>

#include <lib/system/logger.hpp>
#include <csnode/configholder.hpp>

#include <params.hpp>
#include <version.hpp>

#include <sys/types.h>
#include <sys/stat.h>

#include <csdb/storage.hpp>
#include <csdb/pool.hpp>
#include <csdb/database_berkeleydb.hpp>
#include <csdb/database_rocksdb.hpp>

// diagnostic output
#if defined(_MSC_VER)
#if defined(MONITOR_NODE)
#pragma message("\n*** Monitor node has been built ***\n")
#elif defined(WEB_WALLET_NODE)
#pragma message("\n*** Web wallet node has been built ***\n")
#elif defined(SPAMMER)
#pragma message("\n*** Spammer node has been built ***\n")
#else
#pragma message("\n*** Basic node has been built ***\n")
#endif
#endif  // _MSC_VER

const uint32_t CLOSE_TIMEOUT_SECONDS = 10;

void panic() {
    cserror() << "Couldn't continue due to critical errors. "
              << "The node will be closed in "
              << CLOSE_TIMEOUT_SECONDS << " seconds...";
    std::this_thread::sleep_for(
        std::chrono::seconds(CLOSE_TIMEOUT_SECONDS)
    );
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    const char* kDeprecatedDBPath = "test_db";
    const char* kServiceName = "credits_node";
    std::ios_base::sync_with_stdio(false);

    using namespace boost::program_options;
    options_description desc("Allowed options");
    desc.add_options()
        (cmdline::argHelp, "produce this message")
        ("recreate-index", "recreate index.db")
        (cmdline::argSeed, "enter with seed instead of keys")
        (cmdline::argSetBCTop, po::value<uint64_t>(), "all blocks in blockchain with higher sequence will be removed")
        (cmdline::argBalChange, po::value<std::string>(), "prints all changes in account balance in log file")
        ("disable-auto-shutdown", "node will be prohibited to shutdown in case of fatal errors")
        ("version", "show node version")
        ("db-path", po::value<std::string>(), "path to DB (default: \"db/\")")
        ("dbmigrate", "migrate database from BerkeleyDB to RocksDB")
        ("config-file", po::value<std::string>(), "path to configuration file (default: \"config.ini\")")
        ("public-key-file", po::value<std::string>(), "path to public key file (default: \"NodePublic.txt\")")
        ("private-key-file", po::value<std::string>(), "path to private key file (default: \"NodePrivate.txt\")")
        ("dumpkeys", po::value<std::string>(), "dump your public and private keys into a JSON file with the specified name (UNENCRYPTED!)")
        ("encryptkey", "encrypts the private key with password upon startup (if not yet encrypted)")
#ifdef _WIN32
        (cmdline::argInstall,
            po::value<std::string>(),
            "install 'credits_node' service with specified working directory")
        (cmdline::argUninstall, "uninstall 'credits_node' service")
#endif
        (cmdline::argWorkDir, po::value<std::string>(), "set working directory");

    variables_map vm;
    try {
        store(parse_command_line(argc, argv, desc), vm);
        notify(vm);
    }
    catch (unknown_option& e) {
        cserror() << e.what();
        cslog() << desc;
        return EXIT_FAILURE;
    }
    catch (invalid_command_line_syntax& e) {
        cserror() << e.what();
        cslog() << desc;
        return EXIT_FAILURE;
    }
    catch (...) {
        cserror() << "Couldn't parse the arguments";
        cslog() << desc;
        return EXIT_FAILURE;
    }

    if (vm.count(cmdline::argHelp) > 0) {
        cslog() << desc;
        return EXIT_SUCCESS;
    }

    // in case of version option print info and exit
    if (vm.count(cmdline::argVersion) > 0) {
        cslog() << "Node version is " << Config::getNodeVersion() << "." << Config::getMinorNodeVersion();
#ifdef MONITOR_NODE
        cslog() << "Monitor version";
#endif
#ifdef WEB_WALLET_NODE
        cslog() << "Wallet version";
#endif
        cslog() << "Git info:";
        cslog() << "Build SHA1: " << client::Version::GIT_SHA1;
        cslog() << "Date: " << client::Version::GIT_DATE;
        cslog() << "Subject: " << client::Version::GIT_COMMIT_SUBJECT;
        return EXIT_SUCCESS;
    }

    // test db directory, exit if user did not
    // rename old kDeprecatedDBPath and expect
    // to use it as default one
    if (vm.count(cmdline::argDBPath) == 0) {
        // arg is not set, so default dir is not "db_test"
        struct stat info;
        if (stat(kDeprecatedDBPath, &info) == 0) {
            if (info.st_mode & S_IFDIR) {
                cslog() << "Deprecated blockchain path \'"
                        << kDeprecatedDBPath
                        << "\' is in current directory. Please rename it to \'db\' to "
                        << "use it as default storage, or rename to any other not to "
                        << "use at all, then restart your node again";
                return EXIT_FAILURE;
            }
        } 
    }

#ifdef _WIN32
    if (vm.count(cmdline::argInstall) > 0) {
        auto path = std::filesystem::current_path() / "node.exe";
        std::string params = "--";
        params += cmdline::argWorkDir;
        params += "=";
        params += vm[cmdline::argInstall].as<std::string>();
        auto ecode = cs::installService(kServiceName, path.string(), params);
        if (!ecode) {
            cslog() << "Service 'credits_node' installed successfully.";
            return EXIT_SUCCESS;
        }
        cserror() << "Error while installing service 'credits_node': "
                    << ecode.message()
                    << ", value: " << ecode.value();
        return EXIT_FAILURE;
    }

    if (vm.count(cmdline::argUninstall) > 0) {
        auto ecode = cs::uninstallService(kServiceName);
        if (!ecode) {
            cslog() << "Service 'credits_node' uninstalled successfully.";
            return EXIT_SUCCESS;
        }
        cserror() << "Error while uninstalling service 'credits_node': "
            << ecode.message()
            << ", value: " << ecode.value();
        return EXIT_FAILURE;
    }
#endif

    if (!cscrypto::cryptoInit()) {
        std::cout << "Couldn't initialize the crypto library" << std::endl;
        panic();
    }

    if (vm.count(cmdline::argWorkDir) != 0) {
        std::string currentDir = vm[cmdline::argWorkDir].as<std::string>();
#if defined(_WIN32)
        if (!SetCurrentDirectory(currentDir.c_str())) {
            cserror() << "Cannot set working dir " << currentDir;
            panic();
        }
#else
        chdir(currentDir.c_str());
#endif
    }

    auto config = Config::read(vm, true);

    if (!config.isGood()) {
        panic();
    }
    
    // Handle database migration command
    if (vm.count("dbmigrate") > 0) {
        cslog() << "Starting database migration from BerkeleyDB to RocksDB...";
        
        std::string dbPath = config.getPathToDB();
        if (dbPath.empty()) {
            dbPath = "db";
        }
        
        // Rename existing db directory to db_berkeley
        std::string berkeleyPath = dbPath + "_berkeley";
        std::string rocksdbPath = dbPath;
        
        try {
            if (std::filesystem::exists(dbPath)) {
                cslog() << "Renaming " << dbPath << " to " << berkeleyPath;
                std::filesystem::rename(dbPath, berkeleyPath);
            } else {
                cserror() << "Database directory " << dbPath << " does not exist!";
                return EXIT_FAILURE;
            }
            
            std::cout << "\n=== DATABASE MIGRATION ===\n\n";
            std::cout << "Starting migration from BerkeleyDB to RocksDB...\n";
            std::cout << "This process will:\n";
            std::cout << "1. Load your BerkeleyDB blockchain\n";
            std::cout << "2. Copy each block to a new RocksDB as it loads\n";
            std::cout << "3. Show progress every 1000 blocks\n\n";
            
            cslog() << "Opening source BerkeleyDB storage...";
            
            // Force BerkeleyDB for source
#ifdef _WIN32
            _putenv_s("CS_DATABASE_TYPE", "berkeleydb");
#else
            setenv("CS_DATABASE_TYPE", "berkeleydb", 1);
#endif
            
            csdb::Storage sourceStorage;
            
            // Progress tracking for migration
            size_t blocksProcessed = 0;
            size_t totalBlocks = 0;
            bool migrationComplete = false;
            
            // Custom callback to track progress and perform migration
            csdb::Storage::OpenCallback migrationCallback = [&](const csdb::Storage::OpenProgress& progress) {
                blocksProcessed = progress.poolsProcessed;
                
                if (blocksProcessed % 1000 == 0 && blocksProcessed > 0) {
                    std::cout << "\rLoaded " << blocksProcessed << " blocks..." << std::flush;
                }
                
                return false; // Don't cancel
            };
            
            if (!sourceStorage.open(berkeleyPath, migrationCallback)) {
                cserror() << "Failed to open source BerkeleyDB at " << berkeleyPath;
                return EXIT_FAILURE;
            }
            
            totalBlocks = sourceStorage.size();
            std::cout << "\rSource database opened. Found " << totalBlocks << " blocks.\n";
            
            // Create destination directory
            std::filesystem::create_directories(rocksdbPath);
            
            cslog() << "Creating destination RocksDB storage...";
            
            // Force RocksDB for destination
#ifdef _WIN32
            _putenv_s("CS_DATABASE_TYPE", "rocksdb");
#else
            setenv("CS_DATABASE_TYPE", "rocksdb", 1);
#endif
            
            csdb::Storage destStorage;
            if (!destStorage.open(rocksdbPath)) {
                cserror() << "Failed to create destination RocksDB at " << rocksdbPath;
                return EXIT_FAILURE;
            }
            
            std::cout << "Destination storage created. Starting block-by-block migration...\n";
            
            // Perform the actual migration
            size_t migratedCount = 0;
            for (cs::Sequence seq = 0; seq < totalBlocks; ++seq) {
                csdb::Pool pool = sourceStorage.pool_load(seq);
                if (pool.is_valid()) {
                    if (!destStorage.pool_save(pool)) {
                        cserror() << "Failed to save block " << seq << " to RocksDB";
                        return EXIT_FAILURE;
                    }
                    migratedCount++;
                    
                    if (migratedCount % 1000 == 0) {
                        std::cout << "\rMigrated " << migratedCount << "/" << totalBlocks << " blocks..." << std::flush;
                    }
                } else {
                    cswarning() << "Skipping invalid block at sequence " << seq;
                }
            }
            
            std::cout << "\rMigration completed! Migrated " << migratedCount << "/" << totalBlocks << " blocks.\n\n";
            
            cslog() << "Database migration completed successfully";
            cslog() << "Migrated " << migratedCount << " blocks from BerkeleyDB to RocksDB";
            cslog() << "Original BerkeleyDB data preserved in " << berkeleyPath;
            
            return EXIT_SUCCESS;
        }
        catch (const std::exception& e) {
            cserror() << "Migration preparation failed: " << e.what();
            
            // Try to restore original state
            if (std::filesystem::exists(berkeleyPath) && !std::filesystem::exists(dbPath)) {
                cslog() << "Restoring original database directory...";
                std::filesystem::rename(berkeleyPath, dbPath);
            }
            
            return EXIT_FAILURE;
        }
    }

    if (vm.count(cmdline::argSeed) == 0) {
        if (!config.readKeys(vm)) {
            return EXIT_FAILURE;
        }
    }
    else {
        if (!config.enterWithSeed()) {
            return EXIT_FAILURE;
        }
    }

    if (vm.count(cmdline::argDumpKeys) > 0) {
        auto fName = vm[cmdline::argDumpKeys].as<std::string>();
        if (fName.size() > 0) {
            config.dumpJSONKeys(fName);
            cslog() << "Keys dumped to " << fName;
            return EXIT_SUCCESS;
        }
    }

    cs::Peer peer(kServiceName, config, vm);
    int result = peer.executeProtocol();
    return result;
}
