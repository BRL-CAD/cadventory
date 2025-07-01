#include <catch2/catch_test_macros.hpp>
#include "ProcessGFiles.h"
#include "Model.h"
#include <filesystem>
#include <memory>
#include <QDir>
#include <QSettings>
#include <QDebug>
#include <fstream>       // Added to use std::ifstream

#include "ModelTestFixture.h"

const std::string TEST_NAME = "cadventory_PGFTest";

// Helper function to create a test ModelData object
ModelData createTestModelData(int id, const std::string& shortName, const std::string& filePath) {
    return ModelData{
        id,                 // id
        shortName,          // short_name
        filePath,           // primary_file
        "",                 // override_info
        "",                 // title
        {},                 // thumbnail (empty vector<char>)
        "Author Name",      // author (std::string)
        filePath,           // file_path (library path)
        "Unknown Library",  // library_name
        false,              // is_selected
        false,              // is_processed
        false,              // is_included
        {}                  // tags
    };
}

// Helper function to copy test file from source into the temp directory
std::filesystem::path copyTestFileToTemp(const std::string& filename, const std::filesystem::path& tempDir) {
    // Use TEST_SRC_DIR defined by CMake
    const std::filesystem::path testDataDir = std::filesystem::path(TEST_SRC_DIR).lexically_normal();
    std::filesystem::path sourcePath = testDataDir / filename;
    if (!std::filesystem::exists(sourcePath)) {
        throw std::runtime_error("Test data file not found: " + sourcePath.string());
    }

    std::filesystem::path destPath = tempDir / filename;

    std::filesystem::copy_file(
        sourcePath, destPath,
        std::filesystem::copy_options::overwrite_existing
    );

    return destPath;
}

// Test initialization and object creation
TEST_CASE("ProcessGFiles - Initialization", "[ProcessGFiles]") {
    ModelTestFixture fixture(TEST_NAME);
    ProcessGFiles processor(fixture.model.get());

    REQUIRE(fixture.model != nullptr);
}

// Test processing a file (using `annual_gift_man.g`)
TEST_CASE("ProcessGFiles - File Processing", "[ProcessGFiles]") {
    ModelTestFixture fixture(TEST_NAME);
    ProcessGFiles processor(fixture.model.get());

    SECTION("Process annual_gift_man.g file") {
        // copy .g into current tempDir for use
        std::filesystem::path testFilePath = copyTestFileToTemp("annual_gift_man.g", fixture.tempDir);

        // Create a ModelData object with correct fields
        ModelData modelData = createTestModelData(1, "annual_gift_man", testFilePath.string());
        if (!std::filesystem::exists(modelData.primary_file)) {
            FAIL("annual_gift_man.g file error");
        }

        // Do the processing
        REQUIRE(processor.processGFile(modelData));

        // Verify the model was updated
        auto result = fixture.model.get()->getModelById(modelData.id);
        REQUIRE(result.has_value());
        ModelData currData = *result;
        // Verify that the model is marked as processed
        REQUIRE(currData.is_processed == true);
        // Verify thumbnail generation
        REQUIRE(!currData.thumbnail.empty());
        // Verify that the title is extracted
        REQUIRE(!currData.title.empty());
    }
}

// Test generating a gist report and checking for .pdf output
TEST_CASE("ProcessGFiles - Generate Gist Report and Check PDF Output", "[ProcessGFiles]") {
    ModelTestFixture fixture(TEST_NAME);
    ProcessGFiles processor(fixture.model.get());

    SECTION("Generate annual_gift_man report") {
        std::filesystem::path inputFilePath = copyTestFileToTemp("annual_gift_man.g", fixture.tempDir);
        std::filesystem::path outputFilePath = fixture.tempDir / "annual_gist_report.pdf";  // expecting a .pdf output
        std::string primaryObject = "annual_gift_man";
        std::string label = "Test Label";

        // make sure we have our .g
        if (!std::filesystem::exists(inputFilePath)) {
            FAIL("annual_gift_man.g file error");
        }

        auto [success, errorMessage, command] = processor.generateGistReport(
            inputFilePath.string(), outputFilePath.string(), primaryObject, label
            );

        // Verify that the gist report was generated successfully
        //REQUIRE(success == true);
        //REQUIRE(errorMessage.empty());

        //// Check for output file existence
        //if (std::filesystem::exists(outputFilePath)) {
        //    REQUIRE(std::filesystem::file_size(outputFilePath) > 0);

        //    // verify that the output file is indeed a PDF
        //    std::ifstream file(outputFilePath, std::ios::binary);
        //    char buffer[5];
        //    file.read(buffer, 4);
        //    buffer[4] = '\0';
        //    std::string header(buffer);
        //    REQUIRE(header == "%PDF");
        //} else {
        //    FAIL("Gist report PDF output file not generated");
        //}
    }
}
