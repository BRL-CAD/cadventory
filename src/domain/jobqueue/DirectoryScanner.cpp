#include "DirectoryScanner.h"

#include <filesystem>
#include <algorithm>
#include <iostream>
#include <cctype>

namespace fs = std::filesystem;

DirectoryScanner::DirectoryScanner(JobSpoolService* spool) : m_spool(spool) {}


ScanStats DirectoryScanner::scan(const std::string&              root,
                                 const std::vector<std::string>& exts,
                                 long                            maxDepth,
                                 std::uint64_t                   emitEvery,
                                 ProgressFn                      progressCb) {
    ScanStats stats;

    // sanity
    visited.clear();

    // helper: ext filter
    auto matchesExt = [&](const fs::path& p)->bool {
        if (exts.empty())
            // no extension specified - return all files
            return true;

        std::string e = p.extension().string();
        std::transform(e.begin(), e.end(), e.begin(), ::tolower);
        return std::find(exts.begin(), exts.end(), e) != exts.end();
    };

    // DFS stack keeps <dirPath, depthLeft>
    std::vector<std::pair<std::string,long>> stack;
    stack.emplace_back(root, maxDepth);

    while (!stack.empty()) {
        auto [dir, depth] = stack.back();
        stack.pop_back();

        if (dir.empty() || !fs::exists(dir)) continue;

        // resolve symbolic links
        std::string canon;
        try { 
            canon = fs::canonical(dir).string(); 
        } catch (const fs::filesystem_error&) { 
            continue; 
        }

        // avoid cyclic references
        if (!visited.insert(canon).second) 
            continue;

        try {
            for (const auto& entry : fs::directory_iterator(dir)) {
                try {
                    if ((entry.status().permissions() & fs::perms::owner_read) == fs::perms::none)
                        continue;

                    if (fs::is_directory(entry.status())) {
                        // recurse if we've not reaach our depth limit
                        if (depth < 0 || depth > 1)
                            stack.emplace_back(entry.path().string(), depth - 1);
                    } else if (fs::is_regular_file(entry.status()) && matchesExt(entry.path())) {
                        const std::string abs = entry.path().lexically_normal().string();

                        if (m_spool && m_spool->enqueueJob(abs))
                            ++stats.queued;

                        ++stats.scanned;
                        if (progressCb && stats.scanned % emitEvery == 0)
                            progressCb(stats.scanned, stats.queued);
                    }
                } catch (const fs::filesystem_error& e) {
                    LOG_ERR << "WARNING: Unable to access " << entry.path() << " - " << e.what() << LOG_ENDL;
                }
            }
        }
        catch (const fs::filesystem_error&) {
            // handle fs security and/or attributes silently for now..
            // LOG_ERR << "WARNING: Skipping " << dir << " - " << e.what() << LOG_ENDL;
        }
    }

    // we're done, update progress
    if (progressCb) 
        progressCb(stats.scanned, stats.queued);

    // clear out so we can re-index later
    visited.clear();

    return stats;
}
