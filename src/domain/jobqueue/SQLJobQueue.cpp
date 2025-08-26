#include "SQLJobQueue.h"

#include <stdexcept>
#include <cassert>
#include <fstream>
#include <thread>

// enable ""s operator
using namespace std::string_literals;

#ifndef MODEL_DB_INTEGRATION
constexpr const char* CREATE_TABLE =
    "CREATE TABLE IF NOT EXISTS jobs ("
    " id          INTEGER PRIMARY KEY AUTOINCREMENT,"
    " file_id     TEXT NOT NULL,"
    " directive   TEXT NOT NULL,"
    " source_path TEXT,"
    " claimed_by  TEXT,"
    " claimed_at  REAL,"
    " done_at     REAL,"
    " retry_cnt   INTEGER DEFAULT 0,"
    " UNIQUE(file_id, directive)"
    ");"
    "CREATE INDEX IF NOT EXISTS ix_jobs_unclaimed ON jobs(claimed_by);";
#else
constexpr const char* CREATE_TABLE =
    "CREATE TABLE IF NOT EXISTS jobs ("
    " id          INTEGER PRIMARY KEY AUTOINCREMENT,"
    " model_id    INTEGER NOT NULL REFERENCES model(id) ON DELETE CASCADE,"
    " file_id     TEXT NOT NULL,"
    " directive   TEXT NOT NULL,"
    " source_path TEXT,"
    " claimed_by  TEXT,"
    " claimed_at  REAL,"
    " done_at     REAL,"
    " retry_cnt   INTEGER DEFAULT 0,"
    " UNIQUE(model_id, file_id, directive)"
    ");"
    "CREATE INDEX IF NOT EXISTS ix_jobs_unclaimed ON jobs(claimed_by);";
#endif

#ifndef MODEL_DB_INTEGRATION
constexpr const char* INSERT_SQL =
    "INSERT OR IGNORE INTO jobs(file_id,directive,source_path)"
    " VALUES(?1,?2,?3);";
#else
constexpr const char* INSERT_SQL =
    "INSERT OR IGNORE INTO jobs(model_id,file_id,directive,source_path)"
    " VALUES(?1,?2,?3,?4);";
#endif

constexpr const char* CLAIM_SQL =
    "UPDATE jobs "
    "SET claimed_by = ?1, "
    "    claimed_at = CAST(strftime('%s','now') AS REAL) "
    "WHERE id IN ("
    "  SELECT id FROM jobs "
    "  WHERE claimed_by IS NULL "
    "     OR (claimed_by IS NOT NULL "
    "         AND claimed_at IS NOT NULL "
    "         AND (CAST(strftime('%s','now') AS REAL) - claimed_at) > ?2)"
    "  ORDER BY "
    "    CASE WHEN claimed_by IS NULL THEN 0 ELSE 1 END, "  // unclaimed first
    "    id "
    "  LIMIT 1"
    ") "
    "RETURNING id;";

constexpr const char* GET_SQL =
    "SELECT id,file_id,directive,source_path,claimed_at"
    " FROM jobs WHERE id=?1;";

constexpr const char* FINISH_SQL =
    "DELETE FROM jobs WHERE id=?1";
    //"UPDATE jobs SET done_at=strftime('%s','now') WHERE id=?1;";

constexpr const char* RESCUE_SQL =
    "UPDATE jobs SET claimed_by=NULL, claimed_at=NULL, retry_cnt=retry_cnt+1"
    " WHERE claimed_by IS NOT NULL"
    "   AND (strftime('%s','now') - CAST(claimed_at AS REAL)) > ?1;";

constexpr const char* COUNT_SQL =
    "SELECT COUNT(*) FROM jobs;";

#ifndef MODEL_DB_INTEGRATION
SQLJobQueue::SQLJobQueue(const std::filesystem::path& rootDir)
    : m_ownConn(true)
{
    // create db file if we don't already have it
    std::string dbPath = (rootDir / "queue.db").string();
    if (!std::filesystem::exists(dbPath)) {
        std::filesystem::create_directories(rootDir);
        std::ofstream dbFile(dbPath);
    }

    ck(sqlite3_open_v2(dbPath.c_str(), &m_db,
                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                       SQLITE_OPEN_FULLMUTEX, nullptr));
    sqlite3_busy_timeout(m_db, 5000);   // 5s busy timeout
    ck(sqlite3_exec(m_db, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr));
    ck(sqlite3_exec(m_db, "PRAGMA journal_mode=DELETE;", nullptr, nullptr, nullptr));
    ck(sqlite3_exec(m_db, "PRAGMA synchronous=FULL;", nullptr, nullptr, nullptr));
    ck(sqlite3_exec(m_db, "PRAGMA mmap_size=0;", nullptr, nullptr, nullptr));
    ck(sqlite3_exec(m_db, CREATE_TABLE, nullptr, nullptr, nullptr));
    prepare();
}
#else
SQLJobQueue::SQLJobQueue(sqlite3* external)
    : m_db(external), m_ownConn(false)
{
    if (!m_db) throw std::invalid_argument("external db is null");
    ck(sqlite3_exec(m_db, CREATE_TABLE, nullptr, nullptr, nullptr));
    prepare();
}
#endif

SQLJobQueue::~SQLJobQueue() {
    sqlite3_finalize(m_ins);
    sqlite3_finalize(m_clm);
    sqlite3_finalize(m_get);
    sqlite3_finalize(m_fin);
    sqlite3_finalize(m_rqs);
    sqlite3_finalize(m_cnt);
    if (m_ownConn)
        sqlite3_close(m_db);
}


void SQLJobQueue::prepare() {
    ck(sqlite3_prepare_v2(m_db, INSERT_SQL, -1, &m_ins, nullptr));
    ck(sqlite3_prepare_v2(m_db, CLAIM_SQL,  -1, &m_clm, nullptr));
    ck(sqlite3_prepare_v2(m_db, GET_SQL,    -1, &m_get, nullptr));
    ck(sqlite3_prepare_v2(m_db, FINISH_SQL, -1, &m_fin, nullptr));
    ck(sqlite3_prepare_v2(m_db, RESCUE_SQL, -1, &m_rqs, nullptr));
    ck(sqlite3_prepare_v2(m_db, COUNT_SQL,  -1, &m_cnt, nullptr));
}

#ifndef MODEL_DB_INTEGRATION
bool SQLJobQueue::createJob(const std::string& fileId,
                            const std::string& directive,
                            const std::string& sourcePath)
{
    sqlite3_reset(m_ins); sqlite3_clear_bindings(m_ins);
    sqlite3_bind_text(m_ins, 1, fileId.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(m_ins, 2, directive.c_str(),-1, SQLITE_TRANSIENT);
    sqlite3_bind_text(m_ins, 3, sourcePath.c_str(),-1, SQLITE_TRANSIENT);
#else
bool SQLJobQueue::createJob(long long        modelId,
                            const std::string& fileId,
                            const std::string& directive,
                            const std::string& sourcePath)
{
    sqlite3_reset(m_ins); sqlite3_clear_bindings(m_ins);
    sqlite3_bind_int64(m_ins, 1, modelId);
    sqlite3_bind_text (m_ins, 2, fileId.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (m_ins, 3, directive.c_str(),-1, SQLITE_TRANSIENT);
    sqlite3_bind_text (m_ins, 4, sourcePath.c_str(),-1, SQLITE_TRANSIENT);
#endif
    int rc = sqlite3_step(m_ins);
    ck(rc == SQLITE_DONE ? SQLITE_OK : rc);

    // clear statement and release lock
    sqlite3_reset(m_ins);

    // changes == 0 -> row already present; INSERT ignored
    return sqlite3_changes(m_db) > 0;
}

std::optional<JobDescriptor> SQLJobQueue::claimJob(const std::string& workerId) {
    for (int retries = 0; retries < MAX_RETRIES; ++retries) {
        int rc = sqlite3_exec(m_db, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);
        if (rc == SQLITE_BUSY) {
            // try again
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        ck(rc);

        sqlite3_reset(m_clm);
        sqlite3_clear_bindings(m_clm);
        sqlite3_bind_text(m_clm, 1, workerId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(m_clm,  2, m_staleTimeoutSecs);

        rc = sqlite3_step(m_clm);
        if (rc == SQLITE_DONE) {             // queue empty
            sqlite3_reset(m_clm);
            ck(sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr));
            return std::nullopt;
        }

        if (rc == SQLITE_ROW) {
            long long id = sqlite3_column_int64(m_clm, 0);

            rc = sqlite3_step(m_clm);            // advance to SQLITE_DONE
            sqlite3_reset(m_clm);                // release locks
            if (rc != SQLITE_DONE) {
                // something bad happened - rollback
                sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
                if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
                    // try again
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                ck(rc);
            }
            // at this point we should have a claimed, valid row

            ck(sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr));

            // build descriptor
            sqlite3_reset(m_get);
            sqlite3_bind_int64(m_get, 1, id);

            rc = sqlite3_step(m_get);
            if (rc != SQLITE_ROW) {
                sqlite3_reset(m_get);
                return std::nullopt;
            }

            auto safeTxt = [](sqlite3_stmt* st, int idx)->std::string {
                const unsigned char* p = sqlite3_column_text(st, idx);
                return p ? reinterpret_cast<const char*>(p) : "";
            };

            JobDescriptor jd;
            jd.id         = id;
            jd.fileId     = safeTxt(m_get, 1);
            jd.directive  = safeTxt(m_get, 2);
            jd.sourcePath = safeTxt(m_get, 3);
            jd.claimedBy  = workerId;
            jd.claimedAt  = sqlite3_column_double(m_get, 4);

            sqlite3_reset(m_get);
            return jd;
        }

        // unexpected rc: rollback and try again
        sqlite3_reset(m_clm);
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
            // try again
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // final check
        ck(rc);
    }

    return std::nullopt;
}

void SQLJobQueue::finish(const JobDescriptor& jd) {
    int rc, retries = 0;
    do {
        // BEGIN IMMEDIATE obtains the RESERVED lock up front;
        // if someone else holds it we get BUSY here, *not* at DELETE.
        rc = sqlite3_exec(m_db, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);
        if (rc == SQLITE_BUSY) {
            // try again
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            ++retries;
            continue;
        }
        ck(rc);

        sqlite3_reset(m_fin);
        sqlite3_bind_int64(m_fin, 1, jd.id);
        rc = sqlite3_step(m_fin);       // DELETE row
        sqlite3_reset(m_fin);

        // unexpected rc: rollback and try again
        if (rc != SQLITE_DONE) {
            sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
            if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
                // try again
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                ++retries;
                continue;
            }
            ck(rc);
        }

        ck(sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr));
        return;                        // success
    } while (retries < MAX_RETRIES);

    throw std::runtime_error("finish(): database busy");
}

void SQLJobQueue::rescueStale(std::chrono::seconds maxAge) {
    sqlite3_reset(m_rqs);
    sqlite3_bind_int64(m_rqs, 1, maxAge.count());
    ck(sqlite3_step(m_rqs));
    sqlite3_reset(m_rqs);
}

int SQLJobQueue::totalCount() {
    sqlite3_reset(m_cnt);
    int rc = sqlite3_step(m_cnt);

    int cnt = (rc == SQLITE_ROW) ? static_cast<uint64_t>(sqlite3_column_int64(m_cnt, 0)) : 0;

    sqlite3_reset(m_cnt);
    return cnt;
}

void SQLJobQueue::ck(int rc) {
    if (rc != SQLITE_OK && rc != SQLITE_ROW && rc != SQLITE_DONE)
        throw std::runtime_error(sqlite3_errstr(rc));
}