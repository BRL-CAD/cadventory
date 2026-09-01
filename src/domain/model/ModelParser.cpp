#include "ModelParser.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "Logger.h"
#include "config.h"
#include "executeCommand.h"

ModelParser::ModelParser()
    : ModelParser(MGED_EXECUTABLE_PATH) {}

ModelParser::ModelParser(std::string mgedExecutable)
    : m_mgedExecutable(std::move(mgedExecutable)) {}

std::vector<std::string> ModelParser::fetchObjectFiles(const std::string& path) const {
    std::vector<std::string> objectFiles;

    for (int depth = 1; depth < 10; ++depth) {
        const ProcessResult result = runProcess(
            m_mgedExecutable,
            {"-c", path, "search", "/", "-depth", std::to_string(depth)});
        if (!result.success()) {
            LOG_ERR << "mged object search failed for " << path << ": "
                    << result.error << LOG_ENDL;
            continue;
        }

        std::istringstream stream(result.output);
        std::string object;
        while (std::getline(stream, object)) {
            const auto first = object.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
                continue;
            const auto last = object.find_last_not_of(" \t\r\n");
            objectFiles.push_back(object.substr(first, last - first + 1));
        }
    }

    if (objectFiles.empty())
        LOG_ERR << "No objects found while parsing " << path << LOG_ENDL;
    return objectFiles;
}

std::string ModelParser::fetchTitle(const std::string& path) const {
    const ProcessResult result = runProcess(m_mgedExecutable, {"-c", path, "title"});
    if (!result.success()) {
        LOG_ERR << "mged title lookup failed for " << path << ": "
                << result.error << LOG_ENDL;
        return {};
    }
    return result.output;
}

namespace {

std::string nativePath(std::string path) {
#ifndef _WIN32
    std::replace(path.begin(), path.end(), '\\', '/');
    if (path.rfind("C:/", 0) == 0)
        path.replace(0, 2, "/mnt/c");
#endif
    return path;
}

} // namespace

ModelMetadata ModelParser::parseModel(std::string filepath) const {
    const std::string resolvedPath = nativePath(filepath);
    std::ifstream file(resolvedPath);
    if (!file.is_open())
        throw std::invalid_argument("File does not exist: " + filepath);

    ModelMetadata metadata;
    metadata.filepath = filepath;
    metadata.title = fetchTitle(resolvedPath);
    while (!metadata.title.empty() &&
           (metadata.title.back() == '\n' || metadata.title.back() == '\r')) {
        metadata.title.pop_back();
    }
    metadata.objectFiles = fetchObjectFiles(resolvedPath);
    return metadata;
}
