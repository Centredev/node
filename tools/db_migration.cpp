/**
 * @file db_migration.cpp
 * @author Credits Team
 * @brief Database migration utility from BerkeleyDB to RocksDB
 */

#include <iostream>
#include <memory>
#include <string>
#include <filesystem>
#include <cstdlib>

#include <csdb/storage.hpp>
#include <csdb/pool.hpp>

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " <berkeley_db_path> <rocksdb_path>" << std::endl;
    std::cout << "  berkeley_db_path: Path to existing Berkeley DB database" << std::endl;
    std::cout << "  rocksdb_path: Path where RocksDB database will be created" << std::endl;
}

class DatabaseMigrator {
public:
    static bool migrateData(const std::string& berkeleyPath, const std::string& rocksdbPath) {
        std::cout << "Opening source storage (BerkeleyDB)..." << std::endl;
        
        // Set environment to force BerkeleyDB
        setenv("CS_DATABASE_TYPE", "berkeleydb", 1);
        
        csdb::Storage sourceStorage;
        if (!sourceStorage.open(berkeleyPath)) {
            std::cerr << "Error: Cannot open source storage at " << berkeleyPath << std::endl;
            std::cerr << "Make sure the database directory exists and is valid." << std::endl;
            return false;
        }
        
        std::cout << "Source storage opened successfully." << std::endl;
        std::cout << "Found " << sourceStorage.size() << " blocks in source database." << std::endl;
        
        std::cout << "Creating destination storage (RocksDB)..." << std::endl;
        
        // Create destination directory
        std::filesystem::create_directories(rocksdbPath);
        
        // Set environment to force RocksDB
        setenv("CS_DATABASE_TYPE", "rocksdb", 1);
        
        csdb::Storage destStorage;
        if (!destStorage.open(rocksdbPath)) {
            std::cerr << "Error: Cannot create destination storage at " << rocksdbPath << std::endl;
            return false;
        }
        
        std::cout << "Destination storage created successfully." << std::endl;
        std::cout << "Starting migration..." << std::endl;
        
        // Perform the migration
        size_t totalBlocks = sourceStorage.size();
        size_t migratedCount = 0;
        
        for (cs::Sequence seq = 0; seq < totalBlocks; ++seq) {
            csdb::Pool pool = sourceStorage.pool_load(seq);
            if (pool.is_valid()) {
                if (!destStorage.pool_save(pool)) {
                    std::cerr << "Error: Failed to save pool " << seq << " to destination" << std::endl;
                    return false;
                }
                migratedCount++;
                
                if (migratedCount % 1000 == 0) {
                    std::cout << "\rMigrated " << migratedCount << "/" << totalBlocks << " blocks..." << std::flush;
                }
            } else {
                std::cout << "Warning: Invalid pool at sequence " << seq << ", skipping" << std::endl;
            }
        }
        
        std::cout << "\rMigration completed! Migrated " << migratedCount << "/" << totalBlocks << " blocks." << std::endl;
        
        // Clear environment variable
        unsetenv("CS_DATABASE_TYPE");
        
        return true;
    }
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        print_usage(argv[0]);
        return 1;
    }

    const std::string berkeley_path = argv[1];
    const std::string rocksdb_path = argv[2];

    std::cout << "=== Database Migration Utility ===" << std::endl;
    std::cout << "From: " << berkeley_path << " (BerkeleyDB)" << std::endl;
    std::cout << "To:   " << rocksdb_path << " (RocksDB)" << std::endl;
    std::cout << std::endl;

    // Validate paths
    if (!std::filesystem::exists(berkeley_path)) {
        std::cerr << "Error: Source database directory does not exist: " << berkeley_path << std::endl;
        return 1;
    }

    if (std::filesystem::exists(rocksdb_path) && !std::filesystem::is_empty(rocksdb_path)) {
        std::cerr << "Error: Destination directory exists and is not empty: " << rocksdb_path << std::endl;
        std::cerr << "Please remove the destination directory first." << std::endl;
        return 1;
    }

    std::cout << "WARNING: This migration will trigger blockchain initialization." << std::endl;
    std::cout << "This process may take a long time for large databases." << std::endl;
    std::cout << "The tool will show progress every 1000 blocks." << std::endl;
    std::cout << std::endl;

    if (DatabaseMigrator::migrateData(berkeley_path, rocksdb_path)) {
        std::cout << std::endl;
        std::cout << "Migration completed successfully!" << std::endl;
        std::cout << "You can now set database_type=rocksdb in your config.ini" << std::endl;
        return 0;
    } else {
        std::cerr << "Migration failed!" << std::endl;
        std::cout << std::endl;
        std::cout << "Alternative approach:" << std::endl;
        std::cout << "1. Rename your db directory: mv db db_berkeley" << std::endl;
        std::cout << "2. Set database_type=rocksdb in config.ini" << std::endl;
        std::cout << "3. Start the node - it will sync from network" << std::endl;
        return 1;
    }
}