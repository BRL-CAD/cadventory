#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <thread>
#include <atomic>
#include <vector>
#include <cstring>

#include <mutex>
#include <condition_variable>
#include <random>

#include "SQLiteDB.h"
#include "fixtures/TempDirFixture.h"

namespace fs = std::filesystem;
const std::string TEST_NAME = "cadventory_SQLDBTest";

// Small helper to run a scalar SELECT and return int
static int selectInt(SQLiteDB& db, std::string_view sql) {
    int out = 0;
    db.query(sql,
             Binder{},
             [&](sqlite3_stmt* st){
                 out = sqlite3_column_int(st, 0);
                 return true;
             });
    return out;
}

static bool rowExists(SQLiteDB& db, int writerId, int seq) {
    int found = 0;
    db.query("SELECT COUNT(*) FROM t WHERE writer_id=?1 AND seq=?2;",
             [&](sqlite3_stmt* st){
                 sqlite3_bind_int(st, 1, writerId);
                 sqlite3_bind_int(st, 2, seq);
             },
             [&](sqlite3_stmt* st){
                 found = sqlite3_column_int(st, 0);
                 return true;
             });
    return found == 1;
}

TEST_CASE("SQLiteDB opens and closes", "[SQLiteDB]") {
    TempDirFixture fixture(TEST_NAME);
    const std::string dbfile = (fixture.tempDir / "open.db").string();

    // simple constructor call
    REQUIRE_NOTHROW( SQLiteDB(dbfile) );

    // open and report open
    SQLiteDB db(dbfile);
    REQUIRE(db.isOpen());

    // close and report close
    db.close();
    REQUIRE(!db.isOpen());
}

TEST_CASE("SQLiteDB exec & query basic roundtrip", "[SQLiteDB]") {
    TempDirFixture fixture(TEST_NAME);
    const std::string dbfile = (fixture.tempDir / "roundtrip.db").string();

    SQLiteDB db(dbfile);
    REQUIRE(db.isOpen());

    // create simple table
    REQUIRE(db.exec("CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);"));
    // insert a few values
    REQUIRE(db.exec("INSERT INTO t(v) VALUES('a'),('b'),('c');"));

    // query values, make sure we got all three from the INSERT
    int rows = 0;
    REQUIRE(db.query("SELECT v FROM t ORDER BY id;",
                     Binder{},
                     [&](sqlite3_stmt* st){
                         auto* s = sqlite3_column_text(st, 0);
                         REQUIRE(s != nullptr);
                         ++rows;
                         return true; // iterate all rows
                     }));
    REQUIRE(rows == 3);
}

TEST_CASE("SQLiteDB binder with parameters and early-stop row callback", "[SQLiteDB]") {
    TempDirFixture fixture(TEST_NAME);
    const std::string dbfile = (fixture.tempDir / "binder.db").string();

    SQLiteDB db(dbfile);
    REQUIRE(db.exec("CREATE TABLE t(x INTEGER);"));

    // exec with binder
    REQUIRE(db.exec("INSERT INTO t(x) VALUES(?1), (?2), (?3);",
                    [&](sqlite3_stmt* st){
                        sqlite3_bind_int(st, 1, 10);
                        sqlite3_bind_int(st, 2, 20);
                        sqlite3_bind_int(st, 3, 30);
                    }));

    // Early-stop after first two rows
    int sum = 0;
    REQUIRE(db.query("SELECT x FROM t ORDER BY x ASC;",
                    Binder{},
                    [&](sqlite3_stmt* st){
                        sum += sqlite3_column_int(st, 0);
                        return sum < 30; // stop once sum reaches 30 (10+20)
                    }));
    REQUIRE(sum == 30);
}

TEST_CASE("SQLiteDB blob roundtrip", "[SQLiteDB]") {
    TempDirFixture fix(TEST_NAME);
    const std::string dbfile = (fix.tempDir / "blob.db").string();

    SQLiteDB db(dbfile);
    REQUIRE(db.exec("CREATE TABLE t(id INTEGER PRIMARY KEY, b BLOB);"));

    // Write a small blob
    const std::vector<unsigned char> payload{0x89,0x50,0x4E,0x47}; // "PNG"
    REQUIRE(db.writeBlob("INSERT INTO t(b) VALUES (?1);",
                         [&](sqlite3_stmt* st){
                             sqlite3_bind_blob(st, 1, payload.data(), (int)payload.size(), SQLITE_TRANSIENT);
                         }));

    // NULL blob
    REQUIRE(db.exec("INSERT INTO t(b) VALUES(NULL);"));

    // Empty blob (X'')
    REQUIRE(db.exec("INSERT INTO t(b) VALUES(X'');"));

    // Read #1
    {
        auto got = db.readBlob("SELECT b FROM t WHERE id=1;", Binder{});
        REQUIRE(got == payload);
    }
    // Read #2 (NULL -> empty vector)
    {
        auto got = db.readBlob("SELECT b FROM t WHERE id=2;", Binder{});
        REQUIRE(got.empty());
    }
    // Read #3 (empty blob -> empty vector)
    {
        auto got = db.readBlob("SELECT b FROM t WHERE id=3;", Binder{});
        REQUIRE(got.empty());
    }
}

TEST_CASE("SQLiteDB uniqueness violation reports failure (no-throw policy)", "[SQLiteDB]") {
    TempDirFixture fix(TEST_NAME);
    const std::string dbfile = (fix.tempDir / "unique.db").string();

    SQLiteDB db(dbfile);
    REQUIRE(db.exec("CREATE TABLE t(x INTEGER UNIQUE);"));
    REQUIRE(db.exec("INSERT INTO t(x) VALUES(1);"));

    // Second insert violates UNIQUE; ck() returns false; exec() should return false
    REQUIRE_FALSE(db.exec("INSERT INTO t(x) VALUES(1);"));
    // Verify only one row present
    REQUIRE(selectInt(db, "SELECT COUNT(*) FROM t;") == 1);
}

TEST_CASE("SQLiteDB write lock serializes writers (two threads, one DB)", "[sqlite][lock][threads]") {
    TempDirFixture fix(TEST_NAME);
    const std::string dbfile = (fix.tempDir / "lock.db").string();

    SQLiteDB db(dbfile);
    REQUIRE(db.exec("CREATE TABLE t(x INTEGER);"));

    std::atomic<int> okCount{0};

    auto writer = [&](int base){
        bool all_ok = true;
        for (int i=0; i<200; ++i) {
            bool ok = db.exec("INSERT INTO t(x) VALUES(?1);",
                              [&](sqlite3_stmt* st){ sqlite3_bind_int(st, 1, base + i); });
            if (!ok) { all_ok = false; break; }
        }
        if (all_ok) ++okCount;
    };

    std::thread t1(writer, 1000);
    std::thread t2(writer, 2000);
    t1.join(); t2.join();

    REQUIRE(okCount.load() == 2);

    // Expect 400 rows total
    REQUIRE(selectInt(db, "SELECT COUNT(*) FROM t;") == 400);
}

class CountdownLatch {
public:
    explicit CountdownLatch(int count) : count_(count) {}
    void arrive() {
        std::unique_lock<std::mutex> lk(m_);
        if (count_ > 0) --count_;
        if (count_ == 0) cv_.notify_all();
    }
    void wait() {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&]{ return count_ == 0; });
    }
private:
    std::mutex m_;
    std::condition_variable cv_;
    int count_;
};

TEST_CASE("SQLiteDB heavy concurrent writers/readers (C++17, NFS-like contention)",
          "[sqlite][stress][slow]") {
    TempDirFixture fix("SQLiteDB_heavy17");
    const std::string dbfile = (fix.tempDir / "heavy.db").string();

    // ---- Tunables (reduce for faster CI) ----
    constexpr int NUM_WRITERS           = 3;
    constexpr int NUM_READERS           = 2;
    constexpr int WRITES_PER_WRITER     = 200;
    constexpr int READER_ITERATIONS     = 300;
    constexpr int JITTER_US_MAX         = 500;
    constexpr int POST_VERIFY_SAMPLES   = 50;

    // Prepare schema
    {
        SQLiteDB setup(dbfile);
        REQUIRE(setup.isOpen());
        REQUIRE(setup.exec(
            "CREATE TABLE t ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  writer_id INTEGER NOT NULL,"
            "  seq INTEGER NOT NULL,"
            "  payload TEXT"
            ");"
        ));
        REQUIRE(setup.exec(
            "CREATE UNIQUE INDEX idx_writer_seq ON t(writer_id, seq);"
        ));
    }

    std::atomic<int> writersDone{0};
    std::atomic<int> readersDone{0};
    std::atomic<int> writerErrors{0};
    std::atomic<int> readerErrors{0};

    // Independent connections per thread (simulate multi-process NFS clients)
    std::vector<std::unique_ptr<SQLiteDB>> writerConns;
    std::vector<std::unique_ptr<SQLiteDB>> readerConns;
    writerConns.reserve(NUM_WRITERS);
    readerConns.reserve(NUM_READERS);

    for (int i = 0; i < NUM_WRITERS; ++i)
        writerConns.emplace_back(std::make_unique<SQLiteDB>(dbfile));
    for (int i = 0; i < NUM_READERS; ++i)
        readerConns.emplace_back(std::make_unique<SQLiteDB>(dbfile));

    for (auto& w : writerConns) REQUIRE(w->isOpen());
    for (auto& r : readerConns) REQUIRE(r->isOpen());

    // Start gate so all threads begin together (C++17 latch)
    CountdownLatch startGate(NUM_WRITERS + NUM_READERS);

    // RNG/Jitter (thread-local)
    auto jitterDist = std::uniform_int_distribution<int>(0, JITTER_US_MAX);

    // Writers
    std::vector<std::thread> writers;
    writers.reserve(NUM_WRITERS);
    for (int w = 0; w < NUM_WRITERS; ++w) {
        writers.emplace_back([&, w]{
            thread_local std::mt19937 rng{std::random_device{}()};
            auto& db = *writerConns[w];

            startGate.arrive();  // signal ready
            startGate.wait();    // wait for everyone

            for (int i = 0; i < WRITES_PER_WRITER; ++i) {
                const std::string payload = "writer_" + std::to_string(w) + "_seq_" + std::to_string(i);

                // Optional: retry the same row a few times to be extra resilient
                bool success = false;
                for (int attempt = 0; attempt < 3 && !success; ++attempt) {
                    success = db.exec(
                        "INSERT INTO t(writer_id, seq, payload) VALUES(?1, ?2, ?3);",
                        [&](sqlite3_stmt* st){
                            sqlite3_bind_int(st, 1, w);
                            sqlite3_bind_int(st, 2, i);
                            sqlite3_bind_text(st, 3, payload.c_str(), (int)payload.size(), SQLITE_TRANSIENT);
                        }
                    );
                    if (!success) {
                        ++writerErrors;
                        std::this_thread::sleep_for(std::chrono::microseconds(jitterDist(rng)));
                    }
                }

                // Slow-NFS-like jitter
                std::this_thread::sleep_for(std::chrono::microseconds(jitterDist(rng)));
            }
            ++writersDone;
        });
    }

    // Readers
    std::vector<std::thread> readers;
    readers.reserve(NUM_READERS);
    for (int r = 0; r < NUM_READERS; ++r) {
        readers.emplace_back([&, r]{
            thread_local std::mt19937 rng{std::random_device{}()};
            auto& db = *readerConns[r];

            startGate.arrive();  // signal ready
            startGate.wait();    // wait for everyone

            int lastCount = 0;
            for (int k = 0; k < READER_ITERATIONS; ++k) {
                // Snapshot count; OK if stale; should not go backwards
                int cnt = selectInt(db, "SELECT COUNT(*) FROM t;");
                if (cnt < lastCount) {
                    ++readerErrors; // observed non-monotonic snapshot (should not happen)
                } else {
                    lastCount = cnt;
                }

                // Touch a subset of rows to exercise scanning under churn
                db.query("SELECT writer_id, seq FROM t WHERE id % 97 = 0;",
                         Binder{},
                         [&](sqlite3_stmt* st){
                             (void)sqlite3_column_int(st, 0);
                             (void)sqlite3_column_int(st, 1);
                             return true;
                         });

                std::this_thread::sleep_for(std::chrono::microseconds(jitterDist(rng)));
            }
            ++readersDone;
        });
    }

    for (auto& t : writers) t.join();
    for (auto& t : readers) t.join();

    REQUIRE(writersDone.load() == NUM_WRITERS);
    REQUIRE(readersDone.load() == NUM_READERS);

    // Final verification using a fresh connection
    SQLiteDB verify(dbfile);
    REQUIRE(verify.isOpen());

    const int expectedTotal = NUM_WRITERS * WRITES_PER_WRITER;
    const int finalCount = selectInt(verify, "SELECT COUNT(*) FROM t;");
    REQUIRE(finalCount == expectedTotal); // no lost writes

    // Spot-check per-writer presence at boundaries + samples
    for (int w = 0; w < NUM_WRITERS; ++w) {
        std::vector<int> seqs;
        seqs.reserve(2 + POST_VERIFY_SAMPLES);
        seqs.push_back(0);
        seqs.push_back(WRITES_PER_WRITER - 1);
        int cursor = 0;
        for (int s = 0; s < POST_VERIFY_SAMPLES; ++s) {
            cursor = (cursor + 131) % WRITES_PER_WRITER;
            seqs.push_back(cursor);
        }
        for (int seq : seqs) {
            REQUIRE(rowExists(verify, w, seq));
        }
    }

    // Readers should not have seen a decreasing count
    REQUIRE(readerErrors.load() == 0);

    // Writer errors may be >0 under contention; INFO for visibility
    INFO("Writer transient error count (expected >= 0 under contention): " << writerErrors.load());
}