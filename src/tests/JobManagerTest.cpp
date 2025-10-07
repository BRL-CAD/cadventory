#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <chrono>

#include "ModelTestFixture.h"
#include "JobManager.h"
#include "Model.h"

TEST_CASE("JobManager seeds ModelRepo with all .g files", "[JobManager]") {
    // set up temp dir
    ModelTestFixture fixture("cadventory_JMTest");  // creates temp dir and creates empty model db

    // copy .g into test dir
    fixture.copyTestFileToTemp("annual_gift_man.g");
    std::string pathToGiftman = (fixture.tempDir / "annual_gift_man.g").string();

    // need an app so we can start qt threads
    static int argc = 0;
    QCoreApplication app(argc, nullptr);
    // set up our JobManager
    JobManager manager;
    manager.setRootPath(fixture.tempDir.string());

    // let JobManager do its thing
    REQUIRE(manager.start());

    // wait up to ~5s
    auto timelimit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < timelimit) {
	// wait
    }

    // make sure manager is stopped
    manager.stop();

    // verify our .g was loaded into the ModelRepo
    Model repo(fixture.tempDir.string());
    repo.refreshModelData();
    REQUIRE(repo.rowCount() == 1);
    auto loaded_models = repo.getIncludedNotProcessedModels();
    REQUIRE(loaded_models.size() == 1);
}