#include "SQLiteDB.h"
#include "Logger.h"

#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>


SQLiteDB::SQLiteDB(std::string db_path, SQLiteOptions opts)
    : m_db_path(std::move(db_path)), m_lock(std::make_unique<SimpleFileLock>()) {
    // TODO: normalize db_path first?

    // give lock a unique path
    m_lock->setPath(m_db_path + ".lock");

    // open db
    if (sqlite3_open_v2(m_db_path.c_str(), &m_db, opts.open_flags, nullptr) != SQLITE_OK) {
        LOG_ERR << "Can't open database at " << m_db_path << ": " << sqlite3_errmsg(m_db) << LOG_ENDL;
        close();
        return;
    }

    // extended result codes for better debugging
    sqlite3_extended_result_codes(m_db, true);

    // busy timeout
    sqlite3_busy_timeout(m_db, opts.busy_timeout_ms);

    // ensure we have our desired PRAGMA
    bool good_pragmas = true;
    try {
        // foreign_keys on
        good_pragmas = ck(sqlite3_exec(m_db, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr));

        // journal_mode
        std::string jMode = "PRAGMA journal_mode=" + opts.journal_mode + ";";
        good_pragmas &&= ck(sqlite3_exec(m_db, jMode.c_str(), nullptr, nullptr, nullptr));

        // synchronous
        std::string synchronous = "PRAGMA synchronous=" + opts.synchronous + ";";
        good_pragmas &&= ck(sqlite3_exec(m_db, synchronous.c_str(), nullptr, nullptr, nullptr));

        // mmap_size
        good_pragmas &&= ck(sqlite3_exec(m_db, "PRAGMA mmap_size=0;", nullptr, nullptr, nullptr));
    } catch (...) {
        // if our ck() throw's; make sure we don't orphan our db connection and re-throw
        close();
        throw;
    }

    // something went wrong in our creation, make sure we close the connection
    if (!good_pragmas)
        close();
}

SQLiteDB::~SQLiteDB() {
    close();
}

void SQLiteDB::close() {
    if (m_db) {
        sqlite3_close_v2(m_db);
        m_db = nullptr;
    }
}

bool SQLiteDB::query(std::string_view sql, const RowCB& row_cb) const {
    return query(sql, Binder{}, row_cb);
}

bool SQLiteDB::query(std::string_view sql, const Binder& binder, const RowCB& row_cb) const {
    if (!m_db)
        return false;

    sqlite3_stmt* st = nullptr;
    if (!ck(sqlite3_prepare_v2(m_db, std::string(sql).c_str(), -1, &st, nullptr)))
        return false;

    if (binder)
        binder(st);

    // TODO/FIXME: lock for reads?
    int rc = SQLITE_OK;
    for (;;) {
        rc = sqlite3_step(st);
        if (rc == SQLITE_ROW) {
            if (!row_cb(st)) {
                // if callback returns false, stop iterating (but still treat as success)
                sqlite3_finalize(st);
                return true;
            }
        } else if (rc == SQLITE_DONE) {
            sqlite3_finalize(st);
            return true;    // "no rows" is still success
        } else {
            // error?
            bool ok = ck(rc, st);
            sqlite3_finalize(st);   // no-op if ck() already finalized st
            return ok;
        }
    }
}

std::vector<unsigned char>
SQLiteDB::readBlob(std::string_view select_sql, const Binder& binder) const {
    std::vector<unsigned char> out;
    if (!m_db) return out;

    sqlite3_stmt* st = nullptr;
    if (!ck(sqlite3_prepare_v2(m_db, std::string(select_sql).c_str(), -1, &st, nullptr)))
        return out;

    if (binder)
        binder(st);

    // TODO/FIXME: lock for reads?
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(st, 0);
        const int   sz   = sqlite3_column_bytes(st, 0);
        if (blob && sz > 0) {
            const auto* b = static_cast<const unsigned char*>(blob);
            out.assign(b, b + sz);
        }
    }
    (void)ck(rc, st);
    sqlite3_finalize(st);   // no-op if ck() finalizes st

    return out;
}

bool SQLiteDB::exec(std::string_view sql) const {
    if (!m_db) return false;

    // assume we're going to write something - take the lock
    SimpleFileLock::Guard SFLock(*m_lock);
    if (!SFLock) return false;

    // use just sqlite3_exec() to keep it fast
    return ck(sqlite3_exec(m_db, std::string(sql).c_str(), nullptr, nullptr, nullptr));
}

bool SQLiteDB::exec(std::string_view sql, const Binder& binder, ExecInfo* info = nullptr) const {
    return exec(sql, binder, RowCB{}, info);
}

bool SQLiteDB::exec(std::string_view sql, const Binder& binder, const RowCB& row_cb, ExecInfo* info = nullptr) const {
    if (!m_db) return false;

    // we're going to write something - take the lock
    SimpleFileLock::Guard SFLock(*m_lock);
    if (!SFLock) return false;

    sqlite3_stmt* st = nullptr;
    if (!ck(sqlite3_prepare_v2(m_db, std::string(sql).c_str(), -1, &st, nullptr)))
        return false;

    if (binder)
        binder(st);

    // If we have a row_cb, iterate rows; else single-step
    if (row_cb) {
        for (;;) {
            int rc = sqlite3_step(st);
            if (rc == SQLITE_ROW) {
                if (!row_cb(st)) {
                    sqlite3_finalize(st);
                    if (info) {
                        info->changes    = sqlite3_changes(m_db);
                        info->last_rowid = sqlite3_last_insert_rowid(m_db);
                    }
                    return true;
                }
            } else if (rc == SQLITE_DONE) {
                sqlite3_finalize(st);
                if (info) {
                    info->changes    = sqlite3_changes(m_db);
                    info->last_rowid = sqlite3_last_insert_rowid(m_db);
                }
                return true;
            } else {
                const bool ok = ck(rc, st);
                sqlite3_finalize(st);
                return ok;
            }
        }
    } else {
        const int rc  = sqlite3_step(st);
        const bool ok = ck(rc, st);
        sqlite3_finalize(st);
        if (ok && info) {
            info->changes    = sqlite3_changes(m_db);
            info->last_rowid = sqlite3_last_insert_rowid(m_db);
        }
        return ok;
    }

    return false;   // how'd we get here?
}

bool SQLiteDB::writeBlob(std::string_view upsert_sql, const Binder& binder) {
    if (!m_db) return false;

    // take the lock
    SimpleFileLock::Guard SFLock(*m_lock);
    if (!SFLock) return false;

    sqlite3_stmt* st = nullptr;
    if (!ck(sqlite3_prepare_v2(m_db, std::string(upsert_sql).c_str(), -1, &st, nullptr)))
        return false;

    if (binder)
        binder(st);

    bool ok = ck(sqlite3_step(st), st);
    sqlite3_finalize(st);   // no-op if ck() finalizes st

    return ok;
}

bool SQLiteDB::ck(int rc, sqlite3_stmt* st) const {
    if (rc == SQLITE_OK || rc == SQLITE_ROW || rc == SQLITE_DONE)
        return true;    // good return

    const int xrc = m_db ? sqlite3_extended_errcode(m_db) : rc;
    const char* emsg = m_db ? sqlite3_errmsg(m_db) : "";
    const char* estr = sqlite3_errstr(rc);
    const char* dbf  = m_db ? sqlite3_db_filename(m_db, "main") : "";
    const char* sql  = st ? sqlite3_sql(st) : nullptr;

    // good cleanup - finalize statement if we're going to throw
    if (st)
        sqlite3_finalize(st);

    std::ostringstream oss;
    oss << "sqlite error rc=" << rc << " (" << estr << ")"
        << " xrc=" << xrc
        << " db=" << (dbf ? dbf : "");
    if (sql)
        oss << " sql=" << sql;
    oss << " msg=" << (emsg ? emsg : "");
    throw std::runtime_error(oss.str());

    // TODO: LOG instead of throw?
    return false;   // unreachable if we throw
}
