#pragma once
#include <string>
#include <string_view>
#include <functional>
#include <cstdint>
#include <vector>
#include <memory>

#include <sqlite3.h>
#include "SimpleFileLock.h"

typedef std::function<bool(sqlite3_stmt*)> RowCB;
typedef std::function<void(sqlite3_stmt*)> Binder;

struct SQLiteOptions {
    int           busy_timeout_ms = 5000;
    int           open_flags      = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    std::string   journal_mode    = "WAL";
    std::string   synchronous     = "NORMAL";
};

struct ExecInfo {
    int           changes = 0;        // sqlite3_changes(m_db) after the stmt
    sqlite3_int64 last_rowid = 0;     // sqlite3_last_insert_rowid(m_db) after the stmt
};

class SQLiteDB {
public:
    SQLiteDB(std::string db_path, SQLiteOptions opts = {});
    ~SQLiteDB();

    // can't copy; we own things
    SQLiteDB(const SQLiteDB&)		     = delete;
    SQLiteDB& operator=(const SQLiteDB&)     = delete;
    SQLiteDB(SQLiteDb&&) noexcept	     = default;
    SQLiteDB& operator=(SQLiteDB&&) noexcept = default;

    bool isOpen() const noexcept { return m_db != nullptr; }
    void close();   // explicit close (will automatically get called by destructor)

    /* --- reads ---
     * sql   : actual sql query
     * binder: value-plugging the ?1, ?2, etc. part of a query
     * row_cb: callback function for each row returned from query (return false for fast exit)
     * 
     * NOTE: reads do NOT take a writer-lock; use exec() for anything that writes to the db
     */
    bool query(std::string_view sql, const RowCB& row_cb) const;
    bool query(std::string_view sql, const Binder& binder, const RowCB& row_cb) const;

    std::vector<unsigned char> readBlob(std::string_view sql, const Binder& binder) const;

    /* --- writes ---
     * sql     : actual sql query
     * binder  : value-plugging the ?1, ?1, etc. part of a query
     * row_cb  : callback functino for each row returned from a query (return false for fast exit)
     * ExecInfo: (optional) richer data after the stmt
     * 
     * NOTE: writes are protected with a db lock
     */
    bool exec(std::string_view sql) const;
    bool exec(std::string_view sql, const Binder& binder, ExecInfo* info = nullptr) const;
    bool exec(std::string_view sql, const Binder& binder, const RowCB& row_cb, ExecInfo* info = nullptr) const;

    bool writeBlob(std::string_view sql, const Binder& binder);

private:
    bool ck(int rc, sqlite3_stmt* st = nullptr) const;

    std::string                     m_db_path;
    sqlite3*                        m_db{nullptr};
    std::unique_ptr<SimpleFileLock> m_lock{nullptr}; // owned
};
