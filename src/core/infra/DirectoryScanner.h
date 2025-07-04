#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_set>
#include <functional>

// TODO: abstract 'JobService' interface
#include "JobSpoolService.h"

struct ScanStats {
    std::uint64_t scanned = 0;
    std::uint64_t queued  = 0;      // only used if we're constructed with a jobService
};

class DirectoryScanner
{
public:
    explicit DirectoryScanner(JobSpoolService *spool = nullptr);

    using ProgressFn = std::function<void(std::uint64_t scanned, std::uint64_t queued)>;

    ScanStats scan(const std::string              &root,
                   const std::vector<std::string> &exts       = {},             // all files if no extensions supplied
                   long                            maxDepth   = -1,
                   std::uint64_t                   emitEvery  = 1000,
                   ProgressFn                      progressCb = nullptr);

private:
    JobSpoolService                  *m_spool;
    std::unordered_set<std::string>   visited;
};
