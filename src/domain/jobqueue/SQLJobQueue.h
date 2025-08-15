#pragma once

// TODO: started roughing in logic to couple this to a table in the model.db
//#define MODEL_DB_INTEGRATION

#include <filesystem>
#include <string>
#include <optional>
#include <chrono>
#include <sqlite3.h>

struct JobDescriptor {
    long long    id        = -1;      // jobs.rowid
    std::string  fileId;
    std::string  directive;
    std::string  sourcePath;
    std::string  claimedBy;
    double       claimedAt = 0;

    bool valid() const noexcept { return id >= 0; }
};

class SQLJobQueue {
public:
#ifndef MODEL_DB_INTEGRATION
    explicit SQLJobQueue(const std::filesystem::path& rootDir);
#else
    explicit SQLJobQueue(sqlite3* externalDb);  // will reuse models.db handle
#endif

    SQLJobQueue(const SQLJobQueue&)            = delete;
    SQLJobQueue& operator=(const SQLJobQueue&) = delete;
    ~SQLJobQueue();

    // create job with ID, directive and optional original filepath
#ifndef MODEL_DB_INTEGRATION
    bool createJob(const std::string& fileId,
                   const std::string& directive,
                   const std::string& sourcePath = {});
#else
    bool createJob(long long modelId,
                   const std::string& fileId,
                   const std::string& directive,
                   const std::string& sourcePath = {});
#endif

    // return's valid JobDescriptor or std::nullopt if queue is empty
    std::optional<JobDescriptor> claimJob(const std::string& workerId);

    // removes job from db
    void finish(const JobDescriptor& jd);

    // recycle jobs with claimed time > maxAge
    void rescueStale(std::chrono::seconds maxAge);

    // total count of jobs in db
    int totalCount();

private:
    // db connection
    sqlite3*     m_db      = nullptr;
    bool         m_ownConn = false;

    // prepared statements
    sqlite3_stmt* m_ins = nullptr;
    sqlite3_stmt* m_clm = nullptr;
    sqlite3_stmt* m_get = nullptr;
    sqlite3_stmt* m_fin = nullptr;
    sqlite3_stmt* m_rqs = nullptr;
    sqlite3_stmt* m_cnt = nullptr;

    // helpers
    void prepare();
    static void ck(int rc);
};
