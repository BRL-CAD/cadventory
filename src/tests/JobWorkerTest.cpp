// TestJobWorkerIntegration.cpp
#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <string>

#include "ModelTestFixture.h"
#include "JobWorker.h"
#include "Model.h"
#include "SQLJobQueue.h"

TEST_CASE("JobWorker end-to-end pipeline", "[JobWorker]") {
    // set up temp dirs
    ModelTestFixture fixture("cadventory_JWTest");  // creates temp dir and creates empty model db
    auto jobsDir  = fixture.tempDir / ".cadventory" / "jobsdb";
    auto dataDir  = fixture.tempDir / ".cadventory" / "data";
    std::filesystem::create_directories(jobsDir);
    std::filesystem::create_directories(dataDir);

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
    // set up our JobWorker
    JobWorker worker;
    worker.setRootPaths(fixture.tempDir.string(), 
                        jobsDir.string(), 
                        dataDir.string(),
                        fixture.tempDir.string());

    // let JobWorker do its thing
    REQUIRE(worker.start());

    // wait for the model row to flip to processed
    // We'll poll the model DB via a *fresh* Model instance each pass
    // to ensure we see updates the worker writes.
    // give it up to ~5s
    auto timelimit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bool processed = false;
    while (std::chrono::steady_clock::now() < timelimit) {
        Model modelCheck(fixture.tempDir.string());
        ModelData md = modelCheck.getModelByFilePath(pathToGiftman);
        if (md.is_processed) {
            processed = true;
            break;
        }
    }
    REQUIRE(processed);

    // stop worker
    worker.stop();

    // verify queue is empty
    SQLJobQueue qcheck(jobsDir);
    REQUIRE_FALSE(qcheck.claimJob("verify"));

    // verify model row final state
    Model modelCheck(fixture.tempDir.string());
    ModelData md = modelCheck.getModelByFilePath(pathToGiftman);
    REQUIRE(md.is_processed);
    //REQUIRE_FALSE(md.is_processed_dir.empty());

    // verify we have *something* in data/
    int outputFiles = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dataDir)) {
        if (std::filesystem::is_regular_file(entry.status())) {
            ++outputFiles;
        }
    }
    REQUIRE(outputFiles > 0);
}