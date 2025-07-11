#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QSignalSpy>
#include <QThread>
#include <atomic>
#include <string>

#include "ModelTestFixture.h"
#include "QtJobServiceBase.h"

// dummy service for testing qt infrastructure
class DummyService : public QtJobServiceBase
{
public:
    using QtJobServiceBase::QtJobServiceBase;

protected:
    void serviceLoop() override
    {
        std::size_t iterations = 0;
        while (state() == JobServiceState::Running) {
            updateStats([&](JobServiceStats& s) {
                ++s.jobsNew;           // arbitrary counter
            });

            // deterministic break condition (20 x 150ms = ~3sec)
            if (++iterations >= 20)
                break;

            QThread::msleep(150);            // 'work'
        }
    }
};

TEST_CASE("QtJobServiceBase lifecycle and stats", "[jobqueue]") {
    DummyService svc;
    // need an app so we can start qt threads
    static int argc = 0;
    QCoreApplication app(argc, nullptr);

    // spy for statsUpdated signal
    QSignalSpy spy(&svc, &DummyService::statsUpdated);
    REQUIRE(spy.isValid());

    SECTION("Start and stop change state correctly") {
        // should initialize as stopped
        REQUIRE(svc.state() == JobServiceState::Stopped);

        // make sure we start
        REQUIRE(svc.start() == true);

        // give the thread time to emit at least one stats update
        spy.wait(1000);
        REQUIRE_FALSE(spy.empty());
        REQUIRE(svc.state() == JobServiceState::Running);

        // make sure we stop
        svc.stop();
        REQUIRE(svc.state() == JobServiceState::Stopped);
    }

    SECTION("Stats struct is modified via updateStats") {
        REQUIRE(svc.start() == true);

        // ensure spy gets a few updates - 5 sec timeout
        int SPY_EXPECTED = 10;
        QElapsedTimer timer; timer.start();
        while (spy.count() < SPY_EXPECTED) {
            if (timer.elapsed() > 5000)
                FAIL("Timeout waiting for spy");
        }

        // check stats were properly updated
        auto latest = svc.stats();
        REQUIRE(latest.jobsNew >= SPY_EXPECTED);
        REQUIRE(latest.uptime.count() > 0);

        svc.stop();
    }
}
