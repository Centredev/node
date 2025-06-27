/**
 * @file database_rocksdb.hpp
 * @author Credits Team
 */

#ifndef _CREDITS_CSDB_DATABASE_ROCKSDB_H_INCLUDED_
#define _CREDITS_CSDB_DATABASE_ROCKSDB_H_INCLUDED_

#include <memory>
#include <string>

#include <csdb/database.hpp>

namespace rocksdb {
class DB;
class Iterator;
class ColumnFamilyHandle;
class WriteBatch;
struct Options;
struct ReadOptions;
struct WriteOptions;
struct ColumnFamilyOptions;
class Status;
}  // namespace rocksdb

namespace csdb {

class DatabaseRocksDB : public Database {
public:
    DatabaseRocksDB();
    ~DatabaseRocksDB() override;

public:
    bool open(const std::string& path);

private:
    bool is_open() const final;
    bool put(const cs::Bytes& key, uint32_t seq_no, const cs::Bytes& value) final;
    bool get(const cs::Bytes& key, cs::Bytes* value) final;
    bool get(const uint32_t seq_no, cs::Bytes* value) final;
    bool remove(const cs::Bytes& key) final;
    bool seq_no(const cs::Bytes& key, uint32_t* value) final;
    bool write_batch(const ItemList& items) final;
    IteratorPtr new_iterator() final;

    bool updateContractData(const cs::Bytes& key, const cs::Bytes& data) override;
    bool getContractData(const cs::Bytes& key, cs::Bytes& data) override;

private:
    class Iterator;

private:
    void set_last_error_from_rocksdb(const rocksdb::Status& status);
    cs::Bytes seq_no_to_key(uint32_t seq_no) const;

private:
    std::unique_ptr<rocksdb::DB> db_;
    std::unique_ptr<rocksdb::ColumnFamilyHandle> cf_blocks_;
    std::unique_ptr<rocksdb::ColumnFamilyHandle> cf_seq_no_;
    std::unique_ptr<rocksdb::ColumnFamilyHandle> cf_contracts_;
    std::unique_ptr<rocksdb::ReadOptions> read_options_;
    std::unique_ptr<rocksdb::WriteOptions> write_options_;
};

}  // namespace csdb

#endif  // _CREDITS_CSDB_DATABASE_ROCKSDB_H_INCLUDED_