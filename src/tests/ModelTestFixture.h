#ifndef MODEL_TEST_FIXTURE_H
#define MODEL_TEST_FIXTURE_H

#include "Model.h"

#include <filesystem>

namespace fs = std::filesystem;

class ModelTestFixture {
public:
    ModelTestFixture(const std::string& test_name) {
        auto uuid = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        tempDir = std::filesystem::temp_directory_path() / (test_name + "_" + uuid);
        std::filesystem::remove_all(tempDir);   // make sure dir is empty (this is safe even if dir doesn't exist yet)
        std::filesystem::create_directories(tempDir);

        model = std::make_unique<Model>(tempDir.string());
    }

    ~ModelTestFixture() {
        model.reset();  // ensure our model is cleaned up before removing tempDir
        std::filesystem::remove_all(tempDir);
    }

    std::filesystem::path copyTestFileToTemp(const std::string& filename, const std::string& suffix = "") {
        // Use TEST_SRC_DIR defined by CMake
        const std::filesystem::path testDataDir = std::filesystem::path(TEST_SRC_DIR).lexically_normal();
        std::filesystem::path sourcePath = testDataDir / filename;
        if (!std::filesystem::exists(sourcePath)) {
            throw std::runtime_error("Test data file not found: " + sourcePath.string());
        }

        fs::path filename_w_suffix = fs::path(filename).stem().string() 
                                     + suffix 
                                     + fs::path(filename).extension().string();
        std::filesystem::path destPath = tempDir / filename_w_suffix;

        std::filesystem::copy_file(
            sourcePath, destPath,
            std::filesystem::copy_options::overwrite_existing
        );

        return destPath;
    }

    std::filesystem::path tempDir;
    std::unique_ptr<Model> model;
};

#endif // MODEL_TEST_FIXTURE_H