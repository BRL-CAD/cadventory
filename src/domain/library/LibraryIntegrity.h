#pragma once

#include "HiddenDir.h"
#include "ModelTypes.h"

#include <cstddef>
#include <string>
#include <vector>

enum class IntegrityIssueType {
    MissingFile,
    ModifiedFile,
    UnreadableFile,
};

struct IntegrityIssue {
    IntegrityIssueType type;
    int modelId;
    std::string shortName;
    std::string filePath;
    std::string recordedModifiedAt;
    std::string observedModifiedAt;
};

struct IntegrityReport {
    std::size_t checkedModels = 0;
    std::vector<IntegrityIssue> issues;
};

class LibraryIntegrity {
public:
    static IntegrityReport inspect(const HiddenDir& paths,
                                   const std::vector<ModelData>& models);
};
