// SQLJobQueueTest.cpp
#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <iostream>
#include <random>
#include <thread>
#include <vector>
#include <sqlite3.h>

#include "SQLJobQueue.h"

namespace fs = std::filesystem;
const std::string TEST_NAME = "cadventory_SJQTest";


/* helpers */
// creates unique tempdir for tests
// bake in a couple common checks / query helper functions for validation
struct TempDir {
    fs::path root;          // temp dir path
    fs::path dbPath;        // root/queue.db

    explicit TempDir(const std::string& tag) {
        auto stamp = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        root   = fs::temp_directory_path() / (tag + "_" + stamp);
        fs::create_directories(root);
        dbPath = root / "queue.db";
    }
    ~TempDir() { fs::remove_all(root); }

    // open .db RO and run COUNT(*) with an optional WHERE
    int countRows(const std::string& where = "") const {
        sqlite3* db = nullptr;
        if (sqlite3_open_v2(dbPath.string().c_str(), &db,
                            SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
            return 0;       // db may not exist yet
        std::string sql = "SELECT COUNT(*) FROM jobs " + where + ";";
        int rows = 0;
        auto cb = [](void* d,int c,char**v,char**)->int{
                *static_cast<int*>(d) = std::stoi(v[0]);
                (void)c;    // quell unused warning
                return 0; 
            };
        sqlite3_exec(db, sql.c_str(), cb, &rows, nullptr);
        sqlite3_close(db);
        return rows;
    }

    void verifyQueueEmpty() const {
        REQUIRE(countRows("WHERE claimed_by IS NULL")      == 0);
        REQUIRE(countRows("WHERE claimed_by IS NOT NULL")  == 0);
    }

    // debug helper
    void dumpRows() const {
        sqlite3* db = nullptr;
        if (sqlite3_open_v2(dbPath.string().c_str(), &db,
                            SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
            return;

        std::cout << "\n--- jobs table dump (" << dbPath << ") ---\n";
        const char* sql = R"(SELECT * FROM jobs ORDER BY id;)";

        auto cb = [](void*, int col, char** val, char**)->int {
            for (int i = 0; i < col; ++i)
                std::cout << (i ? ", " : "") << (val[i] ? val[i] : "NULL") << '\n';
            return 0;
        };

        sqlite3_exec(db, sql, cb, nullptr, nullptr);
        sqlite3_close(db);
    }
};

// simple worker that will take all jobs and mark finished after a small timeout
class DummyWorker {
public:
    DummyWorker(SQLJobQueue& q, std::string id)
        : queue(q), workerId(std::move(id)) {}
    void run() {
        std::optional<JobDescriptor> job;
        while (true) {
            REQUIRE_NOTHROW(job = queue.claimJob(workerId));

            if (!job)
                break;

            std::this_thread::sleep_for(std::chrono::milliseconds(100));  // 'work'

            ++done;
            REQUIRE_NOTHROW(queue.finish(*job));
        }
    }
    int completed() const { return done.load(); }
private:
    SQLJobQueue&        queue;
    std::string         workerId;
    std::atomic<int>    done{0};
};

/* tests */
TEST_CASE("Create-job behaviour", "[SQLJobQueue]") {
    TempDir tmp(TEST_NAME);
    SQLJobQueue q(tmp.root.string());

    SECTION("dupes and counts", "[SQLJobQueue]") {
        REQUIRE(q.createJob("job1", "dummy"));
        // duplicate should fail
        REQUIRE_FALSE(q.createJob("job1", "dummy"));
        REQUIRE(q.createJob("job2", "dummy"));

        // ensure 2 entries in table, neither claimed
        REQUIRE(tmp.countRows() == 2);
        REQUIRE(tmp.countRows("WHERE claimed_by IS NULL") == 2);

        // claim & finish one -> deletes entry. other is still not claimed
        auto j = q.claimJob("t");
        REQUIRE(j);  
        q.finish(*j);
        REQUIRE(tmp.countRows("WHERE claimed_by IS NULL") == 1);

        // claim last job -> queue should be empty
        j = q.claimJob("w");
        REQUIRE(j);
        q.finish(*j);
        tmp.verifyQueueEmpty();

        // should be nothing left to claim
        REQUIRE_FALSE(q.claimJob("w"));
    }

    SECTION("directive uniqueness", "[SQLJobQueue]") {
        REQUIRE(q.createJob("abc","render"));
        // same fileId, different directive -> allowed
        REQUIRE(q.createJob("abc","print"));
        // same combo again -> duplicate should fail
        REQUIRE_FALSE(q.createJob("abc", "render"));

        // should have just the two jobs
        REQUIRE(tmp.countRows() == 2);
    }

    SECTION("multi-instance queueing", "[SQLJobQueue]") {
        int THREADS = 4;
        int JOBS_PER_THREAD = 100;

        std::atomic<int> inserted{0};
        std::vector<std::thread> pool;

        SECTION("parallel collision", "[SQLJobQueue]") {
            // all threads contend for same key creation
            for (int t = 0; t < THREADS; ++t) {
                pool.emplace_back([&,t]{
                    SQLJobQueue q(tmp.root.string());
                    if (q.createJob("dupFile", "render")) ++inserted;
                });
            }
            for (auto& th : pool) th.join();

            REQUIRE(inserted == 1);                      // at most one row
            REQUIRE(tmp.countRows() == 1);
        }

        SECTION("parallel creation", "[SQLJobQueue]") {
            for (int t = 0; t < THREADS; ++t) {
                pool.emplace_back([&,t]{
                    SQLJobQueue q(tmp.root.string());

                    // let threads queue jobs at the same time
                    for (int i = 0; i < JOBS_PER_THREAD; ++i) {
                        std::string id = "f" + std::to_string(t) + "_" + std::to_string(i);
                        if (q.createJob(id, "task")) 
                            ++inserted;
                    }
                });
            }
            for (auto& th : pool) 
                th.join();

            int expected = THREADS * JOBS_PER_THREAD;
            REQUIRE(inserted == expected);
            REQUIRE(tmp.countRows() == expected);
        }
    }
}

TEST_CASE("Work pipeline behavior (claim -> finish)", "[SQLJobQueue]") {
    constexpr int JOBS      = 100;
    constexpr int THREADS   = 4;

    TempDir tmp(TEST_NAME);
    SQLJobQueue queue(tmp.root.string());

    // seed jobs
    for (int i = 0; i < JOBS; ++i)
        REQUIRE(queue.createJob("job"+std::to_string(i), "dummy"));
    REQUIRE(tmp.countRows() == JOBS);

    SECTION("single-instance") {
        DummyWorker w(queue, "solo");
        w.run();

        REQUIRE(w.completed() == JOBS);
        tmp.verifyQueueEmpty();
    }

    SECTION("multi‑instance") {
        /* NOTE: multi-thread and multi-instance are effectively the same thing
         * since each thread needs its own queue to have its own SQL connection
         */
        std::vector<std::unique_ptr<DummyWorker>> workers;
        std::vector<std::unique_ptr<SQLJobQueue>> queues;
        std::vector<std::thread> threads;

        for (int t = 0; t < THREADS; ++t) {
            queues.emplace_back(std::make_unique<SQLJobQueue>(tmp.root.string()));
            workers.emplace_back(std::make_unique<DummyWorker>(*queues.back(), "w"+std::to_string(t)));
            threads.emplace_back(&DummyWorker::run, workers.back().get());
        }
        for (auto& th:threads) 
            th.join();

        int total=0; 
        for (auto& w:workers) 
            total += w->completed();
        REQUIRE(total == JOBS);
        tmp.verifyQueueEmpty();
    }

    SECTION("New queue joins mid work", "[SQLJobQueue]") {
        DummyWorker wa(queue, "A");
        // start worker 1
        std::thread ta(&DummyWorker::run, &wa);

        // new queue and worker after worker1 is already running
        SQLJobQueue queue1(tmp.root.string());
        DummyWorker wb(queue1,"B");
        // start worker 2
        std::thread tb(&DummyWorker::run, &wb);

        ta.join(); tb.join();

        // verify each got work and all jobs done
        int wa_completed = wa.completed();
        int wb_completed = wb.completed();
        REQUIRE(wa_completed > 0);
        REQUIRE(wb_completed > 0);
        REQUIRE(wa_completed + wb_completed == JOBS);
        tmp.verifyQueueEmpty();
    }
}

TEST_CASE("Finish-job behavior", "[SQLJobQueue]") {
    TempDir tmp(TEST_NAME);
    SQLJobQueue q(tmp.root.string());

    q.createJob("x", "dummy");
    auto job = q.claimJob("w"); 
    REQUIRE(job);

    // first time should actually finish
    REQUIRE_NOTHROW(q.finish(*job));
    tmp.verifyQueueEmpty();

    // second time should be no‑op
    REQUIRE_NOTHROW(q.finish(*job));
    tmp.verifyQueueEmpty();

    // make sure we can recreate the job again
    REQUIRE(q.createJob("x", "dummy"));
}

TEST_CASE("rescueStale behavior", "[SQLJobQueue]") {
    TempDir tmp(TEST_NAME);
    SQLJobQueue q(tmp.root.string());

    SECTION("re-queue old claims", "[SQLJobQueue]") {
        q.createJob("o1","dummy"); 
        q.createJob("o2","dummy");
        auto j1 = q.claimJob("wrk"); REQUIRE(j1);
        auto j2 = q.claimJob("wrk"); REQUIRE(j2);

        // mark j1 as 48h old
        sqlite3* db=nullptr;
        sqlite3_open(tmp.dbPath.string().c_str(),&db);
        sqlite3_exec(db,("UPDATE jobs SET claimed_at=claimed_at-172800 "
                         "WHERE id="+std::to_string(j1->id)).c_str(),
                     nullptr,nullptr,nullptr);
        sqlite3_close(db);

        //tmp.dumpRows();
        q.rescueStale(std::chrono::hours(24));
        //tmp.dumpRows();
        REQUIRE(tmp.countRows("WHERE claimed_by IS NULL") == 1);
        REQUIRE(tmp.countRows("WHERE claimed_by IS NOT NULL") == 1);
    }

    SECTION("increments retry_cnt", "[SQLJobQueue]") {
        q.createJob("f","dummy");
        auto j = q.claimJob("w"); 
        REQUIRE(j);

        // age it
        sqlite3* db=nullptr;
        sqlite3_open(tmp.dbPath.string().c_str(), &db);
        sqlite3_exec(db,("UPDATE jobs SET claimed_at = strftime('%s','now') - 172800 "
                         "WHERE id=" + std::to_string(j->id)).c_str(),
                     nullptr,nullptr,nullptr);
        sqlite3_close(db);

        q.rescueStale(std::chrono::hours(24));

        // row back in queue w/ retry_cnt = 1
        REQUIRE(tmp.countRows("WHERE claimed_by IS NULL") == 1);

        sqlite3_open(tmp.dbPath.string().c_str(), &db);
        int retry=0;
        sqlite3_exec(db, "SELECT retry_cnt FROM jobs WHERE file_id='f';",
            [](void* d,int, char**v,char**)->int{ *(int*)d = std::stoi(v[0]); return 0; },
            &retry, nullptr);
        sqlite3_close(db);
        REQUIRE(retry == 1);
    }
}
