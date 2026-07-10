#pragma once

#include "HiddenDir.h"

#include <filesystem>
#include <string>

struct LibraryBackupResult {
    bool success = false;
    std::filesystem::path directory;
    std::string error;
};

class LibraryBackup {
public:
    static LibraryBackupResult create(const HiddenDir& paths,
                                      const std::filesystem::path& destinationRoot);
};
