// FileJobQueueTest.cpp
#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <thread>
#include <vector>
#include <atomic>
#include <algorithm>    // for count_if

#include "FileJobQueue.h"

namespace fs = std::filesystem;

const std::string TEST_NAME = "cadventory_FJQTest";

class DummyWorker
{
public:
    DummyWorker (FileJobQueue& q, std::string id) : queue(q), workerId(std::move(id)) {}

    void run() {
        //while (auto job = queue.claimJob(workerId)) {
        while (auto job = queue.takeJob()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));        // 'work'

            ++jobsDone;
            //queue.isDone(*job);
            queue.markDone(job.value().jobFile);
        }
    }

    int completed() const { return jobsDone; }

private:
    FileJobQueue& queue;
    std::string workerId;
    std::atomic<int> jobsDone{0};
};

// RAI tempdir
struct TempDir {
    std::filesystem::path path;

    explicit TempDir(const std::string& test_name) {
        auto stamp = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        path = std::filesystem::temp_directory_path() / (test_name + "_" + stamp);
        std::filesystem::remove_all(path);   // make sure dir is empty (this is safe even if dir doesn't exist yet)
        std::filesystem::create_directories(path);
    }

    ~TempDir() { std::filesystem::remove_all(path); }

    // number of regular files under path/<rel> (recursive)
    int countFiles(const fs::path& rel = {}) const {
        fs::path dir = path / rel;
        if (!fs::exists(dir)) 
            return 0;
        return std::count_if(fs::recursive_directory_iterator(dir),
                             fs::recursive_directory_iterator(),
                             [](auto& p) { return p.is_regular_file(); });
    }

    // assert that jobs/<rel>/new & claimed are empty
    void verifyJobsEmpty(const fs::path& jobsRel = "jobs") const {
        REQUIRE(countFiles(jobsRel / "new")     == 0);
        REQUIRE(countFiles(jobsRel / "claimed") == 0);
    }
};


TEST_CASE("Create job behavior", "[FileJobQueue]") {
    TempDir tmp(TEST_NAME);
    FileJobQueue queue(tmp.path);

    SECTION("collision behavior", "[FileJobQueue]") {
        // createJob                        -> should succeed
        REQUIRE(queue.createJob("job1", "dummy"));
        // createJob (with same job name)   -> should fail
        REQUIRE_FALSE(queue.createJob("job1", "dummy"));
        // createJob (with new job name)    -> should succeed
        REQUIRE(queue.createJob("job2", "dummy"));

        // verify we have only 2 new jobs
        REQUIRE(tmp.countFiles(".cadventory/jobs/new") == 2);

        // delete job file - simulate 'done'
        fs::remove_all(tmp.path / "jobs" / "new" / "job1.dummy");
        // try to requeue                   -> should succeed
        REQUIRE(queue.createJob("job1", "dummy"));
        REQUIRE(tmp.countFiles(".cadventory/jobs/new") == 2);
    }
}

TEST_CASE("Claim and Done job queue behavior", "[FileJobQueue]") {
    int NUM_JOBS = 100;
    int NUM_WORKERS = 2;            // 'workers' in a multi environment

    TempDir tmp(TEST_NAME);
    FileJobQueue queue(tmp.path);

    // create n-jobs in temp dir
    for (int i = 0; i < NUM_JOBS; ++i) {
        std::string jobName = "job" + std::to_string(i);
        REQUIRE(queue.createJob(jobName, "dummy"));
    }
    REQUIRE(tmp.countFiles(".cadventory/jobs/new") == NUM_JOBS);

    // execution timer to compare worker run times
    auto start = std::chrono::high_resolution_clock::now();

    SECTION("Process jobs, 1 instance single-threaded", "[FileJobQueue]") {
        DummyWorker w(queue, "w0");
        w.run();

        // log execution time
        auto stop = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(stop - start);
        INFO("1 inst. single-threaded time: " << duration.count() << " seconds");

        // verify worker saw all jobs
        REQUIRE(w.completed() == NUM_JOBS);

        // verify we're empty
        tmp.verifyJobsEmpty();
    }

    SECTION("Process jobs, 1 instnace multi-threaded", "[FileJobQueue]") {
        std::vector<std::unique_ptr<DummyWorker>> workers;
        std::vector<std::thread>                  threads;

        for (int t = 0; t < NUM_WORKERS; ++t) {
            std::string workerId = "w" + std::to_string(t);
            workers.emplace_back(std::make_unique<DummyWorker>(queue, workerId));
            threads.emplace_back(&DummyWorker::run, workers.back().get());
        }

        for (auto& thread : threads) 
            thread.join();

        // log execution time
        auto stop = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(stop - start);
        INFO("1 inst. multi-threaded time: " << duration.count() << " seconds");

        // verify combined workers saw all jobs
        int totalDone = 0;
        for (auto& w : workers) 
            totalDone += w->completed();
        REQUIRE(totalDone == NUM_JOBS);

        // verify empty
        tmp.verifyJobsEmpty();
    }

/*    SECTION("Process jobs, 2 instances concurrently", "[FileJobQueue]") {
        // individual queues, each pointed to same jobs dir
        std::vector<FileJobQueue> queues;
        queues.reserve(NUM_WORKERS);
        queues[0] = queue;
        for (int i = 1; i < NUM_WORKERS; i++) {
            queues.emplace_back(FileJobQueue(tmp.path / "jobs"));
        }

        // each worker gets its own queue
        std::vector<std::unique_ptr<DummyWorker>> workers;
        std::vector<std::thread>                  threads;
        for (int t = 0; t < NUM_WORKERS; ++t) {
            std::string workerId = "w" + std::to_string(t);
            workers.emplace_back(std::make_unique<DummyWorker>(queues[t], workerId));
            threads.emplace_back(&DummyWorker::run, workers.back().get());
        }

        for (auto& thread : threads)
            thread.join();

        // log execution time
        auto stop = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(stop - start);
        INFO("multi instances time: " << duration.count() << " seconds");

        // verify combined workers saw all jobs
        int totalDone = 0;
        for (auto& w : workers) 
            totalDone += w->completed();
        REQUIRE(totalDone == NUM_JOBS);

        // verify empty
        tmp.verifyJobsEmpty();
    }
    */
}

TEST_CASE("rescueStale re-queues orphaned claims", "[FileJobQueue]") {
    TempDir tmp(TEST_NAME);
    FileJobQueue queue(tmp.path);

    // create two jobs and claim
    REQUIRE(queue.createJob("job1", "dummy"));
    REQUIRE(queue.createJob("job2", "dummy"));
    //REQUIRE(queue.claimJob("dummyWorker"));
    REQUIRE(queue.takeJob());
    //auto ret = queue.claimJob("dummyWorker");
    auto ret = queue.takeJob();
    REQUIRE(ret.has_value());

    // back-date one file by 48h
    const auto old_time = fs::file_time_type::clock::now() - std::chrono::hours(24*60);
    //fs::last_write_time(ret.value().pathOnDisk, old_time);
    fs::last_write_time(ret.value().jobFile, old_time);

    // 24h threshold -> one job should be rescued
    //queue.rescueStale(std::chrono::hours(24));
    queue.janitorDirs();

    // ensure one file was recycled to 'new', other remained in 'claimed'
    REQUIRE(tmp.countFiles("jobs/new") == 1);
    REQUIRE(tmp.countFiles("jobs/claimed") == 1);
}
