#pragma once

#include <filesystem>

namespace fs = std::filesystem;

class TempDirFixture {
public:
    TempDirFixture(const std::string& test_name) {
        auto uuid = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        tempDir = fs::temp_directory_path() / (test_name + "_" + uuid);
        fs::remove_all(tempDir);   // make sure dir is empty (this is safe even if dir doesn't exist yet)
        fs::create_directories(tempDir);
    }

    ~TempDirFixture() {
        std::error_code ec;
        std::filesystem::remove_all(tempDir, ec);
    }

    std::filesystem::path copyTestFileToTemp(const std::string& filename, const std::string& suffix = "") {
        // Use TEST_SRC_DIR defined by CMake
        const fs::path testDataDir = fs::path(TEST_SRC_DIR).lexically_normal();
        fs::path sourcePath = testDataDir / filename;
        if (!fs::exists(sourcePath)) {
            throw std::runtime_error("Test data file not found: " + sourcePath.string());
        }

        fs::path filename_w_suffix = fs::path(filename).stem().string() 
                                     + suffix 
                                     + fs::path(filename).extension().string();
        fs::path destPath = tempDir / filename_w_suffix;

        fs::copy_file(
            sourcePath, destPath,
            fs::copy_options::overwrite_existing
        );

        return destPath;
    }

    fs::path tempDir;
};
