#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QSignalSpy>
#include <QThread>
#include <atomic>
#include <string>
#include <memory>
#include <chrono>
#include <thread>

#include "ModelTestFixture.h"
#include "QtJobServiceBase.h"
#include "IJobService.h"
#include "JobManager.h"
#include "JobWorker.h"

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

// dummy factory to select between manager or worker job service
static std::unique_ptr<IJobService> makeJobService(bool manager) {
    if (manager)
        return std::make_unique<JobManager>();
    else
        return std::make_unique<JobWorker>();
}

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

TEST_CASE("Full Job manager+worker pipeline", "[jobService]") {
    // set up temp dirs
    ModelTestFixture fixture("cadventory_JWTest");  // creates temp dir and creates empty model db
    auto jobsDir  = fixture.tempDir / ".cadventory" / "jobsdb";
    auto dataDir  = fixture.tempDir / ".cadventory" / "data";
    std::filesystem::create_directories(jobsDir);
    std::filesystem::create_directories(dataDir);

    // create a few copies of our .g
    const int NUM_G_COPIES = 5;
    std::vector<std::filesystem::path> g_names;
    for (int i = 0; i < NUM_G_COPIES; i++) {
        g_names.push_back(fixture.copyTestFileToTemp("annual_gift_man.g", "_" + std::to_string(i)));
    }

    // need an app so we can start qt threads
    static int argc = 0;
    QCoreApplication app(argc, nullptr);

    // helper
    auto pumpQt = [](std::chrono::seconds secs) -> void {
        auto timelimit = std::chrono::steady_clock::now() + secs;
        while (std::chrono::steady_clock::now() < timelimit) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    };

    // 'factory' setup JobWorker and JobManager
    auto manager = makeJobService(true);
    auto worker  = makeJobService(false);

    REQUIRE(manager);
    REQUIRE(worker);

    // set paths
    const std::string rootStr = fixture.tempDir.string();
    manager->setRootPaths(rootStr, jobsDir.string(), dataDir.string(), rootStr);
    worker->setRootPaths(rootStr, jobsDir.string(), dataDir.string(), rootStr);

    // run the manager, give it ~5s to index and load ModelRepo
    REQUIRE(manager->start());
    pumpQt(std::chrono::seconds(5));
    manager->stop();

    // run the worker, give it ~5s to process
    REQUIRE(worker->start());
    pumpQt(std::chrono::seconds(10));
    worker->stop();

    // verify the models were successfully processed in repo
    // even though it's copies of the same .g, each should have an entry and be processed
    Model repo(rootStr);
    for (const auto& path : g_names) {
        ModelData md = repo.getModelByFilePath(path.string());
        REQUIRE(md.id > 0);
        REQUIRE(md.is_processed);
    }

    // verify we have output from the worker directives
    // since all our models were copies, they should all hash the same
    // i.e. we should only have one output directory
    int outputFiles = 0;
    int outputDirs = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dataDir)) {
        if (std::filesystem::is_regular_file(entry.status())) {
            ++outputFiles;
        }
        if (std::filesystem::is_directory(entry)) {
            ++outputDirs;
        }
    }
    REQUIRE(outputFiles > 0);
    REQUIRE(outputDirs == 1);
}
