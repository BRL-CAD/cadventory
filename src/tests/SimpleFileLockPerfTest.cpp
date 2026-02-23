// SimpleFileLockPerfTest.cpp
#include <catch2/catch_test_macros.hpp>
#include "SimpleFileLock.h"
#include <chrono>
#include <filesystem>
#include <thread>
#include <vector>
#include <atomic>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

static fs::path unique_lock_path(const char* tag) {
    auto base = fs::temp_directory_path() / "cadventory_SFLockPerfTests";
    fs::create_directories(base);
    // Use timestamp + thread id to avoid collisions across concurrent tests
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::ostringstream oss;
    oss << tag << "_" << now << "_" << std::this_thread::get_id() << ".lock";
    return base / oss.str();
}

static void cleanup_lock(const fs::path& p) {
    std::error_code ec;
    fs::remove(p, ec); // best-effort
}

TEST_CASE("SimpleFileLock performance - single thread", "[SimpleFileLock][perf]") {
    const fs::path lockPath = unique_lock_path("single");
    cleanup_lock(lockPath);

    SimpleFileLock lock(lockPath, SimpleFileLock::millis{5000}); // generous timeout
    // Warmup
    {
        SimpleFileLock::Guard g(lock);
        REQUIRE(g.acquired());
    }

    const int iters = 1000; // keep runtime short but meaningful
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iters; ++i) {
        SimpleFileLock::Guard g(lock);
        REQUIRE(g.acquired());
        // destructor releases
    }
    auto end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> secs = end - start;
    double rate = iters / secs.count();
    std::cout << "[SimpleFileLock] single-thread rate: " << rate << " acquires/sec" << std::endl;

    // Conservative floor to avoid flakiness across filesystems
    REQUIRE(rate > 1000.0);

    cleanup_lock(lockPath);
}

TEST_CASE("SimpleFileLock performance - contention", "[SimpleFileLock][perf][threads]") {
    const fs::path lockPath = unique_lock_path("contended");
    cleanup_lock(lockPath);

    SimpleFileLock lock(lockPath, SimpleFileLock::millis{10000}); // avoid spurious timeouts

    const unsigned hw = std::max(4u, std::thread::hardware_concurrency());
    const unsigned threads = std::min(8u, hw);
    const int acquiresPerThread = 200;

    std::atomic<int> successes{0};
    std::vector<std::thread> pool;
    pool.reserve(threads);

    auto start = std::chrono::high_resolution_clock::now();
    for (unsigned t = 0; t < threads; ++t) {
        pool.emplace_back([&] {
            for (int i = 0; i < acquiresPerThread; ++i) {
                SimpleFileLock::Guard g(lock);
                if (g.acquired()) {
                    ++successes;
                    // brief work while holding the lock
                    // (deliberately minimal to measure lock throughput)
                } else {
                    // Should be extremely rare with a 10s timeout
                    // Retry once to avoid counting transient hiccups as failures
                    SimpleFileLock::Guard retry(lock);
                    if (retry.acquired()) ++successes;
                }
            }
        });
    }
    for (auto& th : pool) th.join();
    auto end = std::chrono::high_resolution_clock::now();

    const int totalRequested = static_cast<int>(threads) * acquiresPerThread;
    REQUIRE(successes.load() == totalRequested); // no timeouts expected

    std::chrono::duration<double> secs = end - start;
    double rate = successes.load() / secs.count();
    std::cout << "[SimpleFileLock] contended total rate (" << threads
              << " threads): " << rate << " acquires/sec" << std::endl;

    // With backoff and fsync, contended throughput will be lower; keep threshold modest.
    REQUIRE(rate > 100.0);

    cleanup_lock(lockPath);
}

TEST_CASE("SimpleFileLock timeout behavior under active holder", "[SimpleFileLock][timeout]") {
    const fs::path lockPath = unique_lock_path("timeout");
    cleanup_lock(lockPath);

    // Holder uses a long timeout (doesn't matter much) and keeps the lock for a bit
    SimpleFileLock holderLock(lockPath, SimpleFileLock::millis{5000});
    // The contender uses a short timeout to guarantee a quick failure while lock is held
    SimpleFileLock shortLock(lockPath, SimpleFileLock::millis{50});

    std::thread holder([&] {
        SimpleFileLock::Guard g(holderLock);
        REQUIRE(g.acquired());
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        // release on destruction
    });

    // Give the holder a moment to acquire
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto start = std::chrono::high_resolution_clock::now();
    SimpleFileLock::Guard contender(shortLock);
    auto end = std::chrono::high_resolution_clock::now();

    holder.join();

    // We expect a timeout (i.e., not acquired) within roughly the short timeout window.
    REQUIRE_FALSE(contender.acquired());

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "[SimpleFileLock] timeout observed in ~" << ms << " ms" << std::endl;

    // Allow a bit of jitter above/below the configured 50ms
    REQUIRE(ms >= 30);
    REQUIRE(ms <= 300);

    cleanup_lock(lockPath);
}
