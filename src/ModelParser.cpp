#include "ModelParser.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <array>
#include <memory>
#include <algorithm>
#include <sstream>
#include "executeCommand.h"

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS  
#include <cstdio>               
#endif

ModelParser::ModelParser() {
    // Constructor implementation (if needed)
}

/**
* @brief Fetches object files from a BRL-CAD model file
* 
* @param path Path to the BRL-CAD model file
* @return A vector of object file names
*/
std::vector<std::string> fetchObjectFiles(const std::string& path) {
    std::vector<std::string> objectFiles;

    for (int i = 1; i < 10; i++) {
        std::string command = "mged -c \"" + path + "\" search / -depth " + std::to_string(i);
        std::string output = executeCommandNoWindow(command);

        // Ensure output lines are properly split
        std::istringstream objStream(output);
        std::string obj;

        while (std::getline(objStream, obj)) {
            // Trim whitespace
            obj.erase(0, obj.find_first_not_of(" \t\r\n"));
            obj.erase(obj.find_last_not_of(" \t\r\n") + 1);

            // Skip empty lines
            if (!obj.empty()) {
                objectFiles.push_back(obj);
            }
        }
    }

    // Debugging: Final check on the collected object files
    if (objectFiles.empty()) {
        std::cerr << "ERROR: Object files vector is still empty after parsing!" << std::endl;
    }

    return objectFiles;
}


std::string fetchTitle(const std::string& path) {
    return executeCommandNoWindow("mged -c \"" + path + "\" title 2>&1");
}

std::string convertToUnixPath(const std::string& windowsPath) {
    std::string unixPath = windowsPath;
    std::replace(unixPath.begin(), unixPath.end(), '\\', '/'); 
    if (unixPath.find("C:/") == 0) {
        unixPath.replace(0, 2, "/mnt/c");
    }
    return unixPath;
}

ModelMetadata ModelParser::parseModel(std::string filepath) {
    ModelMetadata metadata;

    std::string fixedPath;

    #ifdef _WIN32
        fixedPath = filepath;
    #else
        // If running on Linux (WSL), convert the Windows path to Unix format
        fixedPath = (filepath.find("C:/") == 0) ? convertToUnixPath(filepath) : filepath;
    #endif

    // fidn if the file exists
    std::ifstream
    file(fixedPath);
    if (!file.is_open()) {
        throw std::invalid_argument("File does not exist");
    }

    metadata.filepath = filepath;

    metadata.title = fetchTitle(filepath);
    metadata.title.erase(metadata.title.find_last_not_of("\n\r") + 1);

    metadata.objectFiles = fetchObjectFiles(filepath);

    return metadata;
}