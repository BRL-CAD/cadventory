#include <catch2/catch_test_macros.hpp>
#include "FilesystemIndexer.h"
#include <chrono>
#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class TempDir {
public:
    fs::path dir;

    TempDir() {
	const std::vector<std::string> exts = {".g", ".cpp", ".h", ".png", ".jpg", ".txt"};

	dir = fs::temp_directory_path() / "cadventory_FIPTest";
	fs::create_directories(dir);

	// build a simple hierarchy
	for (int d = 0; d < m_numDirs; ++d) {
	    auto currDir = dir / ("d" + std::to_string(d));
	    fs::create_directories(currDir);

	    // add nFiles into each dir
	    for (int f = 0; f < m_filesPerDir; ++f) {
		const auto& ext = exts[f % exts.size()];
		std::ofstream(currDir / ("f" + std::to_string(f) + ext));
	    }
	}
    }

    ~TempDir() {
	fs::remove_all(dir);
    }

private:
    // populate 20 * 100 = 2000 files
    int m_numDirs = 20;
    int m_filesPerDir = 100;
};

TEST_CASE("FilesystemIndexer Performance", "[FilesystemIndexer]") {
    // create a simple test directory
    TempDir temp;

    FilesystemIndexer indexer;
    // once to prime
    size_t files = indexer.indexDirectory(temp.dir.generic_string().c_str());

    SECTION("test indexDirectory") {
	auto start = std::chrono::high_resolution_clock::now();
	files = indexer.indexDirectory(temp.dir.generic_string().c_str());
	auto end = std::chrono::high_resolution_clock::now();

	std::chrono::duration<double, std::milli> duration = end - start;
	auto rate = files / (duration.count() / 1000.0);
	std::cout << "Index Rate is " << rate << " files/sec" << std::endl;
	std::cout << "Indexing " << files << " files took " << duration.count() << " ms" << std::endl;

	// Check if the indexing meets our performance criteria
	REQUIRE(rate > 10000); // 10k files/sec
    }

    SECTION("test findFilesWithSuffix") {
	std::vector<std::string> suffixes = {".cpp", ".h", ".png", ".jpg", ".txt"};

	auto start = std::chrono::high_resolution_clock::now();
	auto files = indexer.findFilesWithSuffixes(suffixes);
	auto end = std::chrono::high_resolution_clock::now();

	std::chrono::duration<double, std::milli> duration = end - start;
	auto rate = files.size() / (duration.count() / 1000.0);
	std::cout << "Find rate is " << rate << " files/sec" << std::endl;
	std::cout << "Finding files with given suffixes took " << duration.count() << " ms" << std::endl;

	// Check if file finding meets our criteria
	REQUIRE(rate > 100000); // 100k files/sec
    }
}
