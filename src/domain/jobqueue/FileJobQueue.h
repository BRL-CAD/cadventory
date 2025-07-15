#pragma once

#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <optional>
#include <mutex>
#include <random>
#include <chrono>

namespace fs = std::filesystem;

struct ClaimedJob
{
    fs::path jobFile;      // .../cur/aa/bb/file.job.xxxx
    fs::path origFilepath; // absolute path to real model
};

class FileJobQueue
{
public:
    explicit FileJobQueue(const fs::path& libraryRoot);
    ~FileJobQueue();

    /* --------- queue API --------- */
    bool enqueueJob(const fs::path& filePath);
    bool createJob(const std::string& jobId, const std::string& jobType);
    std::optional<ClaimedJob> takeJob();
    std::size_t takeBatch(std::size_t maxJobs, std::vector<ClaimedJob>& out);
    bool markDone(const fs::path& curJobPath);

    /* --------- maintenance helpers (manual) --------- */
    void janitorDirs();     // recycle timed-out jobs + age-out done/
    void pruneEmptyDirs();  // delete now-empty bucket dirs

private:
    /* --------- helpers --------- */
    static std::string hashPath(const std::string& abs);
    static std::string hashDirs(const std::string& abs);

    /* --------- data --------- */
    const fs::path  m_libRoot;
    const fs::path  m_jobsRoot;

    std::mutex                              m_mutex;
    std::unordered_set<std::string>         m_doneCache; // hashed paths
    std::unordered_map<fs::path,fs::path>   m_jobMap;    // jobFile -> orig
    std::mt19937_64                         m_rng{std::random_device{}()};
    std::uint32_t                           m_lastJanitorBucketIdx{0};

    /* --------- constants --------- */
    static inline const char* JOBS_DIR  = ".cadventory/jobs";
    static inline const char* NEW_DIR   = "new";
    static inline const char* CUR_DIR   = "cur";
    static inline const char* DONE_DIR  = "done";

    static constexpr auto CUR_JOB_TIMEOUT   = std::chrono::hours(1);   // 60 min
    static constexpr auto DONE_CLEANUP_TIME = std::chrono::hours(24*14); // 14 days
    static constexpr int  JANITOR_BUCKETS_PER_PASS = 256;              // 256 × 256 = 65 536 buckets
};
