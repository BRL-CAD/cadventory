// TestJobWorkerIntegration.cpp
#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QSettings>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

#include "ModelTestFixture.h"
#include "JobWorker.h"
#include "Model.h"
#include "SQLJobQueue.h"

TEST_CASE("JobWorker end-to-end pipeline", "[JobWorker]") {
    // set up temp dirs
    ModelTestFixture fixture("cadventory_JWTest");  // creates temp dir and creates empty model db
    HiddenDir paths;
    paths.setLibraryRoot(fixture.tempDir.string());

    // copy .g into test dir
    fixture.copyTestFileToTemp("annual_gift_man.g");
    std::string pathToGiftman = (fixture.tempDir / "annual_gift_man.g").string();

    // ** simulate JobManager loading empty model into db **
    ModelData newModel = {
                            0,                  // id
                            "",                 // short_name
                            pathToGiftman,      // primary_file
                            "",                 // override_info
                            "",                 // title
                            {},                 // thumbnail (empty vector<char>)
                            "",                 // author (std::string)
                            pathToGiftman,      // file_path (library path)
                            "Unknown Library",  // library_name
                            false,              // is_selected
                            false,              // is_processed
                            true,               // is_included
                            {}                  // tags
                         };
    REQUIRE(fixture.model.get()->insertModel(newModel));

    // need an app so we can start qt threads
    static int argc = 0;
    QCoreApplication app(argc, nullptr);
    QCoreApplication::setOrganizationName("BRL-CAD");
    QCoreApplication::setApplicationName("CADventoryTest");

    // make sure we have one thread for the worker
    QSettings().setValue("jobs/numThreads", 1);
    QSettings().sync();
    // set up our JobWorker
    JobWorker worker;
    worker.setRootPath(paths.libRoot());

    // let JobWorker do its thing
    REQUIRE(worker.start());

    auto countOutputFiles = [&paths]() {
        int count = 0;
        if (!std::filesystem::exists(paths.dataDir()))
            return count;

        for (const auto& entry : std::filesystem::recursive_directory_iterator(paths.dataDir())) {
            if (entry.is_regular_file())
                ++count;
        }
        return count;
    };

    SQLJobQueue qcheck(paths.jobsDir());
    int outputFiles = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (std::chrono::steady_clock::now() < deadline) {
        outputFiles = countOutputFiles();
        if (outputFiles > 0 && qcheck.totalCount() == 0)
            break;

        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // stop worker
    worker.stop();

    // verify queue is empty
    REQUIRE_FALSE(qcheck.claimJob("verify"));

    // verify model row final state
    Model modelCheck(fixture.tempDir.string());
    ModelData md = modelCheck.getModelByFilePath(pathToGiftman);
    REQUIRE(md.is_processed);
    //REQUIRE_FALSE(md.is_processed_dir.empty());

    // verify we have *something* in data/
    REQUIRE(outputFiles > 0);
}
