#pragma once

#include <QString>
#include <vector>

/* abstract interface for collection of Models */
struct IModelRepository
{
    virtual ~IModelRepository() = default;

    // sync a batch of filepaths that have been marked done
    virtual void markDoneBatch(const std::vector<QString>& filePaths) = 0;

    // retrieve batch of filepaths that need to be processed
    virtual std::vector<QString> fetchPendingBatch() = 0;
};
