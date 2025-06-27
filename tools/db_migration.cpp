/**
 * @file db_migration.cpp
 * @author Credits Team
 * @brief Utility to migrate data from Berkeley DB to RocksDB
 */

#include <iostream>
#include <memory>
#include <string>
#include <cstring>

#include <csdb/database_berkeleydb.hpp>
#include <csdb/database_rocksdb.hpp>
#include <lib/system/progressbar.hpp>

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " <berkeley_db_path> <rocksdb_path>" << std::endl;
    std::cout << "  berkeley_db_path: Path to existing Berkeley DB database" << std::endl;
    std::cout << "  rocksdb_path: Path where RocksDB database will be created" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        print_usage(argv[0]);
        return 1;
    }

    const std::string berkeley_path = argv[1];
    const std::string rocksdb_path = argv[2];

    std::cout << "Database Migration Utility" << std::endl;
    std::cout << "From: " << berkeley_path << " (Berkeley DB)" << std::endl;
    std::cout << "To:   " << rocksdb_path << " (RocksDB)" << std::endl;
    std::cout << std::endl;

    // Open Berkeley DB
    auto berkeley_db = std::make_unique<csdb::DatabaseBerkeleyDB>();
    if (!berkeley_db->open(berkeley_path)) {
        std::cerr << "Error: Failed to open Berkeley DB at " << berkeley_path << std::endl;
        std::cerr << "Error: " << berkeley_db->last_error_message() << std::endl;
        return 1;
    }
    std::cout << "Successfully opened Berkeley DB" << std::endl;

    // Create RocksDB
    auto rocks_db = std::make_unique<csdb::DatabaseRocksDB>();
    if (!rocks_db->open(rocksdb_path)) {
        std::cerr << "Error: Failed to create RocksDB at " << rocksdb_path << std::endl;
        std::cerr << "Error: " << rocks_db->last_error_message() << std::endl;
        return 1;
    }
    std::cout << "Successfully created RocksDB" << std::endl;

    // Count total entries (approximate)
    size_t total_entries = 0;
    auto count_iter = berkeley_db->new_iterator();
    if (count_iter) {
        count_iter->seek_to_first();
        while (count_iter->is_valid()) {
            total_entries++;
            count_iter->next();
        }
    }
    std::cout << "Total entries to migrate: " << total_entries << std::endl;

    // Migrate data
    std::cout << "\nMigrating data..." << std::endl;
    
    cs::ProgressBar progress_bar(total_entries);
    size_t migrated = 0;
    size_t failed = 0;
    
    auto iter = berkeley_db->new_iterator();
    if (!iter) {
        std::cerr << "Error: Failed to create iterator for Berkeley DB" << std::endl;
        return 1;
    }

    iter->seek_to_first();
    while (iter->is_valid()) {
        uint32_t seq_no = iter->key();
        cs::Bytes value = iter->value();
        
        // Get the block hash for this sequence number
        cs::Bytes hash;
        if (berkeley_db->get(seq_no, &hash)) {
            // Write to RocksDB
            if (!rocks_db->put(hash, seq_no, value)) {
                failed++;
                std::cerr << "\nWarning: Failed to migrate entry with seq_no " << seq_no << std::endl;
            } else {
                migrated++;
            }
        } else {
            failed++;
            std::cerr << "\nWarning: Failed to read entry with seq_no " << seq_no << std::endl;
        }
        
        progress_bar.update(migrated + failed);
        iter->next();
    }

    progress_bar.finish();
    
    std::cout << "\nMigration completed:" << std::endl;
    std::cout << "  Successfully migrated: " << migrated << " entries" << std::endl;
    if (failed > 0) {
        std::cout << "  Failed: " << failed << " entries" << std::endl;
    }

    // Verify migration by comparing a few entries
    std::cout << "\nVerifying migration..." << std::endl;
    
    size_t verified = 0;
    size_t to_verify = std::min(size_t(100), migrated);
    
    auto verify_iter = berkeley_db->new_iterator();
    verify_iter->seek_to_first();
    
    while (verify_iter->is_valid() && verified < to_verify) {
        uint32_t seq_no = verify_iter->key();
        cs::Bytes berkeley_value = verify_iter->value();
        
        cs::Bytes rocks_value;
        if (rocks_db->get(seq_no, &rocks_value)) {
            if (berkeley_value != rocks_value) {
                std::cerr << "Error: Data mismatch for seq_no " << seq_no << std::endl;
                return 1;
            }
            verified++;
        }
        
        verify_iter->next();
    }
    
    std::cout << "Successfully verified " << verified << " entries" << std::endl;
    std::cout << "\nMigration completed successfully!" << std::endl;
    
    return 0;
}