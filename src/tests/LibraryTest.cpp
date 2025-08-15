#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include "Library.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>

// Helper function to create test files in a specified directory
// Each file is populated with basic content for testing
void createTestFiles(const std::string& directory, const std::vector<std::string>& filenames) {
    for (const auto& filename : filenames) {
        std::filesystem::path filePath = std::filesystem::path(directory) / filename;
        std::ofstream outfile(filePath);
        outfile << "Test content for " << filename << std::endl;
        outfile.close();
    }
}

void createModelDB(const std::string& root, const std::vector<std::string>& filenames) {
    Model model(root);

    for (const auto& filename : filenames) {
        ModelData md{};
        md.short_name = filename;
        md.file_path = (std::filesystem::path(root) / filename).generic_string();

        model.insertModel(md);
    }
}

// Helper function to clean up test directories by removing them entirely
void cleanupTestDirectory(const std::string& path) {
    if (std::filesystem::exists(path)) {
        std::filesystem::remove_all(path);
    }
}

// Test cases for the Library class
TEST_CASE("Library Operations", "[Library]") {
    // Create a temporary directory for testing
    std::string testDir = (std::filesystem::temp_directory_path() / "cadventory_library_test").string();
    cleanupTestDirectory(testDir); // Ensure the directory is clean
    std::filesystem::create_directories(testDir);

    // List of mock files to test various library operations
    std::vector<std::string> testFiles = {
        "model1.g",
        "model2.g",
        "geometry1.stl",
        "geometry2.obj",
        "image1.jpg",
        "image2.png",
        "document1.pdf",
        "document2.txt",
        "data1.csv",
        "data2.json",
        "otherfile.xyz"
    };

    // Create the mock files in the test directory
    //createTestFiles(testDir, testFiles);
    createModelDB(testDir, testFiles);

    // NOTE: encapsulate Library so it can go out of scope and release the model and filesystem index for cleanup
    {
        // Initialize the Library instance
        Library library("TestLibrary", testDir.c_str());

        SECTION("Library Initialization") {
            // Verify the library's name and path are correctly initialized
            REQUIRE(std::string(library.name()) == "TestLibrary");
            REQUIRE(std::string(library.path()) == testDir);
        }

        SECTION("Indexing Files") {
            // Verify that the library correctly indexes all files
            size_t indexedFiles = library.indexFiles();
            REQUIRE(indexedFiles == testFiles.size());
        }

        SECTION("Get Models") {
            // Verify the library identifies model files correctly
            auto models = library.getModels();
            REQUIRE(models.size() == 2);

            // Ensure the detected model files match the expected files
            std::vector<std::string> expectedModels = {"model1.g", "model2.g"};
            REQUIRE(std::is_permutation(models.begin(), models.end(), expectedModels.begin()));
        }

        SECTION("Get Geometry Files") {
            // Verify the library retrieves all geometry files correctly
            auto geometryFiles = library.getGeometry();
        
            std::vector<std::string> expectedGeometryFiles = {"model1.g", "model2.g", "geometry1.stl", "geometry2.obj"};
        
            // Extract filenames from the returned paths
            std::vector<std::string> geometryFileBasenames;
            for (const auto& file : geometryFiles) {
                geometryFileBasenames.push_back(std::filesystem::path(file).filename().string());
            }

            // Verify the filenames match the expected geometry files
            REQUIRE(std::is_permutation(geometryFileBasenames.begin(), geometryFileBasenames.end(), expectedGeometryFiles.begin()));
        }

        SECTION("Get Image Files") {
            // Verify the library retrieves all image files correctly
            auto imageFiles = library.getImages();
        
            std::vector<std::string> expectedImageFiles = {"image1.jpg", "image2.png"};
        
            // Extract filenames from the returned paths
            std::vector<std::string> imageFileBasenames;
            for (const auto& file : imageFiles) {
                imageFileBasenames.push_back(std::filesystem::path(file).filename().string());
            }

            // Verify the filenames match the expected image files
            REQUIRE(std::is_permutation(imageFileBasenames.begin(), imageFileBasenames.end(), expectedImageFiles.begin()));
        }

        SECTION("Get Document Files") {
            // Verify the library retrieves all document files correctly
            auto documentFiles = library.getDocuments();
        
            std::vector<std::string> expectedDocumentFiles = {"document1.pdf", "document2.txt"};
        
            // Extract filenames from the returned paths
            std::vector<std::string> documentFileBasenames;
            for (const auto& file : documentFiles) {
                documentFileBasenames.push_back(std::filesystem::path(file).filename().string());
            }

            // Verify the filenames match the expected document files
            REQUIRE(std::is_permutation(documentFileBasenames.begin(), documentFileBasenames.end(), expectedDocumentFiles.begin()));
        }

        SECTION("Get Data Files") {
            // Verify the library retrieves all data files correctly
            auto dataFiles = library.getData();

            std::vector<std::string> expectedDataFiles = {"data1.csv", "data2.json", "otherfile.xyz"};

            // Extract filenames from the returned paths
            std::vector<std::string> dataFileBasenames;
            for (const auto& file : dataFiles) {
                dataFileBasenames.push_back(std::filesystem::path(file).filename().string());
            }

            // Verify the filenames match the expected data files
            REQUIRE(std::is_permutation(dataFileBasenames.begin(), dataFileBasenames.end(), expectedDataFiles.begin()));
        }

        SECTION("Load Database") {
            // Verify the library loads its database without throwing exceptions
            REQUIRE_NOTHROW(library.loadDatabase());
        }
    }

    // Clean up the test directory after all tests
    cleanupTestDirectory(testDir);
}