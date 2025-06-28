/**
 * @file database_rocksdb.cpp
 * @author Credits Team
 */

#include <csdb/database_rocksdb.hpp>

#include <rocksdb/db.h>
#include <rocksdb/iterator.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/status.h>
#include <rocksdb/write_batch.h>

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace csdb {

namespace {
constexpr const char* kDefaultColumnFamily = "default";
constexpr const char* kBlocksColumnFamily = "blocks";
constexpr const char* kSeqNoColumnFamily = "seq_no";
constexpr const char* kContractsColumnFamily = "contracts";

rocksdb::Slice to_slice(const cs::Bytes& bytes) {
    return rocksdb::Slice(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

cs::Bytes from_slice(const rocksdb::Slice& slice) {
    return cs::Bytes(reinterpret_cast<const uint8_t*>(slice.data()), 
                     reinterpret_cast<const uint8_t*>(slice.data() + slice.size()));
}

}  // namespace

class DatabaseRocksDB::Iterator : public Database::Iterator {
public:
    Iterator(rocksdb::DB* db, rocksdb::ColumnFamilyHandle* cf)
        : db_(db)
        , cf_(cf)
        , iter_(db->NewIterator(rocksdb::ReadOptions(), cf)) {
    }

    ~Iterator() override = default;

    bool is_valid() const override {
        return iter_->Valid();
    }

    void seek_to_first() override {
        iter_->SeekToFirst();
    }

    void seek_to_last() override {
        iter_->SeekToLast();
    }

    void seek(const cs::Bytes& key) override {
        iter_->Seek(to_slice(key));
    }

    void seek(const uint32_t seq_no) override {
        cs::Bytes key(sizeof(uint32_t));
        std::memcpy(key.data(), &seq_no, sizeof(uint32_t));
        iter_->Seek(to_slice(key));
    }

    void next() override {
        iter_->Next();
    }

    void prev() override {
        iter_->Prev();
    }

    uint32_t key() const override {
        if (!iter_->Valid()) {
            return 0;
        }
        rocksdb::Slice key_slice = iter_->key();
        if (key_slice.size() != sizeof(uint32_t)) {
            return 0;
        }
        uint32_t result;
        std::memcpy(&result, key_slice.data(), sizeof(uint32_t));
        return result;
    }

    cs::Bytes value() const override {
        if (!iter_->Valid()) {
            return cs::Bytes();
        }
        return from_slice(iter_->value());
    }

private:
    rocksdb::DB* db_;
    rocksdb::ColumnFamilyHandle* cf_;
    std::unique_ptr<rocksdb::Iterator> iter_;
};

DatabaseRocksDB::DatabaseRocksDB()
    : Database() {
}

DatabaseRocksDB::~DatabaseRocksDB() {
    if (db_) {
        // Delete column family handles before closing DB
        cf_contracts_.reset();
        cf_seq_no_.reset();
        cf_blocks_.reset();
        db_.reset();
    }
}

bool DatabaseRocksDB::open(const std::string& path) {
    rocksdb::Options options;
    options.create_if_missing = true;
    options.create_missing_column_families = true;
    
    // Performance optimizations
    options.IncreaseParallelism();
    options.OptimizeLevelStyleCompaction();
    options.max_open_files = 1000;
    options.keep_log_file_num = 10;
    options.max_log_file_size = 10 * 1024 * 1024;  // 10MB
    
    // Column families
    std::vector<rocksdb::ColumnFamilyDescriptor> column_families;
    column_families.push_back(rocksdb::ColumnFamilyDescriptor(
        kDefaultColumnFamily, rocksdb::ColumnFamilyOptions()));
    column_families.push_back(rocksdb::ColumnFamilyDescriptor(
        kBlocksColumnFamily, rocksdb::ColumnFamilyOptions()));
    column_families.push_back(rocksdb::ColumnFamilyDescriptor(
        kSeqNoColumnFamily, rocksdb::ColumnFamilyOptions()));
    column_families.push_back(rocksdb::ColumnFamilyDescriptor(
        kContractsColumnFamily, rocksdb::ColumnFamilyOptions()));
    
    std::vector<rocksdb::ColumnFamilyHandle*> handles;
    rocksdb::DB* db_raw;
    
    rocksdb::Status status = rocksdb::DB::Open(options, path, column_families, &handles, &db_raw);
    
    if (!status.ok()) {
        // Try to open without existing column families
        status = rocksdb::DB::Open(options, path, &db_raw);
        if (!status.ok()) {
            set_last_error_from_rocksdb(status);
            return false;
        }
        
        // Create column families
        rocksdb::ColumnFamilyHandle* cf;
        status = db_raw->CreateColumnFamily(rocksdb::ColumnFamilyOptions(), kBlocksColumnFamily, &cf);
        if (status.ok()) handles.push_back(cf);
        
        status = db_raw->CreateColumnFamily(rocksdb::ColumnFamilyOptions(), kSeqNoColumnFamily, &cf);
        if (status.ok()) handles.push_back(cf);
        
        status = db_raw->CreateColumnFamily(rocksdb::ColumnFamilyOptions(), kContractsColumnFamily, &cf);
        if (status.ok()) handles.push_back(cf);
    }
    
    db_.reset(db_raw);
    
    // Assign column family handles
    for (auto* handle : handles) {
        std::string name = handle->GetName();
        if (name == kBlocksColumnFamily) {
            cf_blocks_.reset(handle);
        } else if (name == kSeqNoColumnFamily) {
            cf_seq_no_.reset(handle);
        } else if (name == kContractsColumnFamily) {
            cf_contracts_.reset(handle);
        } else if (name == kDefaultColumnFamily) {
            // Default column family, managed by RocksDB
            delete handle;
        }
    }
    
    read_options_.reset(new rocksdb::ReadOptions());
    write_options_.reset(new rocksdb::WriteOptions());
    write_options_->sync = true;  // Ensure durability
    
    set_last_error();
    return true;
}

bool DatabaseRocksDB::is_open() const {
    return db_ != nullptr;
}

bool DatabaseRocksDB::put(const cs::Bytes& key, uint32_t seq_no, const cs::Bytes& value) {
    if (!is_open()) {
        set_last_error(NotOpen);
        return false;
    }
    
    rocksdb::WriteBatch batch;
    
    // Write to blocks column family
    batch.Put(cf_blocks_.get(), to_slice(key), to_slice(value));
    
    // Write to seq_no column family
    cs::Bytes seq_key = seq_no_to_key(seq_no);
    batch.Put(cf_seq_no_.get(), to_slice(seq_key), to_slice(value));
    
    rocksdb::Status status = db_->Write(*write_options_, &batch);
    if (!status.ok()) {
        set_last_error_from_rocksdb(status);
        return false;
    }
    
    set_last_error();
    return true;
}

bool DatabaseRocksDB::get(const cs::Bytes& key, cs::Bytes* value) {
    if (!is_open()) {
        set_last_error(NotOpen);
        return false;
    }
    
    std::string result;
    rocksdb::Status status = db_->Get(*read_options_, cf_blocks_.get(), to_slice(key), &result);
    
    if (status.IsNotFound()) {
        set_last_error(NotFound);
        return false;
    }
    
    if (!status.ok()) {
        set_last_error_from_rocksdb(status);
        return false;
    }
    
    if (value) {
        *value = cs::Bytes(result.begin(), result.end());
    }
    
    set_last_error();
    return true;
}

bool DatabaseRocksDB::get(const uint32_t seq_no, cs::Bytes* value) {
    if (!is_open()) {
        set_last_error(NotOpen);
        return false;
    }
    
    cs::Bytes key = seq_no_to_key(seq_no);
    std::string result;
    rocksdb::Status status = db_->Get(*read_options_, cf_seq_no_.get(), to_slice(key), &result);
    
    if (status.IsNotFound()) {
        set_last_error(NotFound);
        return false;
    }
    
    if (!status.ok()) {
        set_last_error_from_rocksdb(status);
        return false;
    }
    
    if (value) {
        *value = cs::Bytes(result.begin(), result.end());
    }
    
    set_last_error();
    return true;
}

bool DatabaseRocksDB::remove(const cs::Bytes& key) {
    if (!is_open()) {
        set_last_error(NotOpen);
        return false;
    }
    
    rocksdb::Status status = db_->Delete(*write_options_, cf_blocks_.get(), to_slice(key));
    if (!status.ok()) {
        set_last_error_from_rocksdb(status);
        return false;
    }
    
    set_last_error();
    return true;
}

bool DatabaseRocksDB::seq_no(const cs::Bytes& key, uint32_t* value) {
    if (!is_open()) {
        set_last_error(NotOpen);
        return false;
    }
    
    // This requires scanning through seq_no column family to find the key
    // In a production system, you might want to maintain a reverse index
    auto iter = db_->NewIterator(*read_options_, cf_seq_no_.get());
    std::unique_ptr<rocksdb::Iterator> iter_guard(iter);
    
    for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
        std::string stored_value = iter->value().ToString();
        cs::Bytes stored_bytes(stored_value.begin(), stored_value.end());
        
        // Check if this value matches our key (simplified comparison)
        // In production, you'd need proper value parsing
        if (stored_bytes == key) {
            rocksdb::Slice key_slice = iter->key();
            if (key_slice.size() == sizeof(uint32_t)) {
                std::memcpy(value, key_slice.data(), sizeof(uint32_t));
                set_last_error();
                return true;
            }
        }
    }
    
    set_last_error(NotFound);
    return false;
}

bool DatabaseRocksDB::write_batch(const ItemList& items) {
    if (!is_open()) {
        set_last_error(NotOpen);
        return false;
    }
    
    rocksdb::WriteBatch batch;
    
    for (const auto& item : items) {
        batch.Put(cf_blocks_.get(), to_slice(item.first), to_slice(item.second));
    }
    
    rocksdb::Status status = db_->Write(*write_options_, &batch);
    if (!status.ok()) {
        set_last_error_from_rocksdb(status);
        return false;
    }
    
    set_last_error();
    return true;
}

DatabaseRocksDB::IteratorPtr DatabaseRocksDB::new_iterator() {
    if (!is_open()) {
        return nullptr;
    }
    
    return std::make_shared<Iterator>(db_.get(), cf_seq_no_.get());
}

bool DatabaseRocksDB::updateContractData(const cs::Bytes& key, const cs::Bytes& data) {
    if (!is_open()) {
        set_last_error(NotOpen);
        return false;
    }
    
    rocksdb::Status status = db_->Put(*write_options_, cf_contracts_.get(), to_slice(key), to_slice(data));
    if (!status.ok()) {
        set_last_error_from_rocksdb(status);
        return false;
    }
    
    set_last_error();
    return true;
}

bool DatabaseRocksDB::getContractData(const cs::Bytes& key, cs::Bytes& data) {
    if (!is_open()) {
        set_last_error(NotOpen);
        return false;
    }
    
    std::string result;
    rocksdb::Status status = db_->Get(*read_options_, cf_contracts_.get(), to_slice(key), &result);
    
    if (status.IsNotFound()) {
        set_last_error(NotFound);
        return false;
    }
    
    if (!status.ok()) {
        set_last_error_from_rocksdb(status);
        return false;
    }
    
    data = cs::Bytes(result.begin(), result.end());
    set_last_error();
    return true;
}

void DatabaseRocksDB::set_last_error_from_rocksdb(const rocksdb::Status& status) {
    if (status.ok()) {
        set_last_error();
    } else if (status.IsNotFound()) {
        set_last_error(NotFound, status.ToString());
    } else if (status.IsCorruption()) {
        set_last_error(Corruption, status.ToString());
    } else if (status.IsNotSupported()) {
        set_last_error(NotSupported, status.ToString());
    } else if (status.IsInvalidArgument()) {
        set_last_error(InvalidArgument, status.ToString());
    } else if (status.IsIOError()) {
        set_last_error(IOError, status.ToString());
    } else {
        set_last_error(UnknownError, status.ToString());
    }
}

cs::Bytes DatabaseRocksDB::seq_no_to_key(uint32_t seq_no) const {
    cs::Bytes key(sizeof(uint32_t));
    std::memcpy(key.data(), &seq_no, sizeof(uint32_t));
    return key;
}

}  // namespace csdb