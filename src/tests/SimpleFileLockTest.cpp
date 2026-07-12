#include <catch2/catch_test_macros.hpp>
#include "SimpleFileLock.h"

#include <filesystem>
#include <thread>
#include <chrono>
#include <sstream>

namespace fs = std::filesystem;

static fs::path unique_lock_path(const char* tag) {
    auto base = fs::temp_directory_path() / "cadventory_SimpleFileLockTests";
    std::error_code ec;
    fs::create_directories(base, ec);

    // timestamp + thread id for uniqueness
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::ostringstream oss;
    oss << tag << "_" << now << "_" << std::this_thread::get_id() << ".lock";
    return base / oss.str();
}

static void cleanup_lock(const fs::path& p) {
    std::error_code ec;
    fs::remove(p, ec); // best-effort
}

TEST_CASE("SimpleFileLock acquires and releases (RAII)", "[SimpleFileLock]") {
    const fs::path lockPath = unique_lock_path("basic");
    cleanup_lock(lockPath);

    SimpleFileLock lock(lockPath, SimpleFileLock::millis{500});
    {
        SimpleFileLock::Guard g(lock);
        REQUIRE(g.acquired());
        REQUIRE(fs::exists(lockPath));
    } // release on scope exit

    // guard released -> lock file should be gone
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    REQUIRE_FALSE(fs::exists(lockPath));

    cleanup_lock(lockPath);
}

TEST_CASE("SimpleFileLock zero timeout makes one non-blocking attempt", "[SimpleFileLock]") {
    const fs::path lockPath = unique_lock_path("zero_timeout");
    cleanup_lock(lockPath);

    SimpleFileLock lock(lockPath, SimpleFileLock::millis{0});
    {
        SimpleFileLock::Guard guard(lock);
        REQUIRE(guard.acquired());
        REQUIRE(fs::exists(lockPath));
    }

    REQUIRE_FALSE(fs::exists(lockPath));
}

TEST_CASE("SimpleFileLock enforces mutual exclusion with timeout", "[SimpleFileLock]") {
    const fs::path lockPath = unique_lock_path("mutex");
    cleanup_lock(lockPath);

    SimpleFileLock holderLock(lockPath, SimpleFileLock::millis{3000});
    SimpleFileLock shortLock(lockPath,  SimpleFileLock::millis{50}); // contender should time out quickly

    std::thread holder([&]{
        SimpleFileLock::Guard g(holderLock);
        REQUIRE(g.acquired());
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // hold for a bit
    });

    // give the holder a head-start
    std::this_thread::sleep_for(std::chrono::milliseconds(15));

    // while holder is active, a short-timeout acquire should fail
    SimpleFileLock::Guard contender(shortLock);
    REQUIRE_FALSE(contender.acquired());

    holder.join();

    // after the holder releases, a new acquire should succeed
    SimpleFileLock again(lockPath, SimpleFileLock::millis{500});
    SimpleFileLock::Guard g2(again);
    REQUIRE(g2.acquired());

    cleanup_lock(lockPath);
}

TEST_CASE("SimpleFileLock can reacquire after release", "[SimpleFileLock]") {
    const fs::path lockPath = unique_lock_path("reacquire");
    cleanup_lock(lockPath);

    SimpleFileLock lock(lockPath, SimpleFileLock::millis{500});

    {
        SimpleFileLock::Guard g1(lock);
        REQUIRE(g1.acquired());
        REQUIRE(fs::exists(lockPath));
    } // released

    REQUIRE_FALSE(fs::exists(lockPath));

    {
        SimpleFileLock::Guard g2(lock);
        REQUIRE(g2.acquired());
        REQUIRE(fs::exists(lockPath));
    } // released

    REQUIRE_FALSE(fs::exists(lockPath));

    cleanup_lock(lockPath);
}

TEST_CASE("SimpleFileLock works after default construction + setPath", "[SimpleFileLock]") {
    const fs::path lockPath = unique_lock_path("setpath");
    cleanup_lock(lockPath);

    SimpleFileLock lock; // default
    lock.setPath(lockPath);
    lock.setAcquireTimeout(SimpleFileLock::millis{250});

    SimpleFileLock::Guard g(lock);
    REQUIRE(g.acquired());
    REQUIRE(fs::exists(lockPath));

    // release & cleanup
    // (guard dtor will remove the file)
    cleanup_lock(lockPath);
}
