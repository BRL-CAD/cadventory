#include "LibraryIntegrity.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace {

std::string formatTimestampUtc(std::time_t value) {
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &value);
#else
    gmtime_r(&value, &tm);
#endif

    std::ostringstream output;
    output << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

std::string modifiedTimeForPath(const std::filesystem::path& filePath,
                                std::error_code& error) {
    const auto modified = std::filesystem::last_write_time(filePath, error);
    if (error)
        return {};

    const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        modified - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    return formatTimestampUtc(std::chrono::system_clock::to_time_t(systemTime));
}

std::filesystem::path resolvePath(const HiddenDir& paths, const std::string& filePath) {
    const std::filesystem::path storedPath(filePath);
    return storedPath.is_absolute() ? storedPath : std::filesystem::path(paths.libRoot()) / storedPath;
}

}

IntegrityReport LibraryIntegrity::inspect(const HiddenDir& paths,
                                          const std::vector<ModelData>& models) {
    IntegrityReport report;
    report.checkedModels = models.size();

    for (const ModelData& model : models) {
        const std::filesystem::path filePath = resolvePath(paths, model.file_path);
        std::error_code error;
        const bool exists = std::filesystem::exists(filePath, error);
        if (error || !exists) {
            report.issues.push_back({IntegrityIssueType::MissingFile, model.id, model.short_name,
                                     filePath.generic_string(), model.modified_at_fs, {}});
            continue;
        }

        const std::string observedModifiedAt = modifiedTimeForPath(filePath, error);
        if (error) {
            report.issues.push_back({IntegrityIssueType::UnreadableFile, model.id, model.short_name,
                                     filePath.generic_string(), model.modified_at_fs, {}});
        } else if (!model.modified_at_fs.empty() && model.modified_at_fs != observedModifiedAt) {
            report.issues.push_back({IntegrityIssueType::ModifiedFile, model.id, model.short_name,
                                     filePath.generic_string(), model.modified_at_fs, observedModifiedAt});
        }
    }

    return report;
}
