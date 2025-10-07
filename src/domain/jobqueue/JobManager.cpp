#include "JobManager.h"
#include "Logger.h"

#include <filesystem>

#include "FilesystemIndexer.h"
#include "Model.h"

static std::string norm(const std::string& p) {
    // always forward slashes
    return std::filesystem::path(p).lexically_normal().generic_string();
}

static ModelData minimalModelData(const std::string& filePath) {
    ModelData md{};
    md.short_name   = std::filesystem::path(filePath).filename().string();
    md.file_path    = norm(filePath);       // use generic_string so we always have '/' separators
    md.is_processed = false;
    md.is_included  = true;                 // new files are included by default

    return md;
}

void JobManager::serviceLoop() {
    const std::string root = paths().libRoot(); // inherit from QtJobServiceBase
    const long        maxDepth = 4;             // default depth
    std::size_t UPDATE_EVERY = 1000;            // update stats emit every n-repo inserts
    std::size_t inserted = 0, handled = 0;      // keep track for periodic stat updates

    Model repo(root);
    repo.loadModelsFromDatabase();
    const bool firstRun = (repo.rowCount() == 0);

    // scan filesystem
    FilesystemIndexer indexer(root.c_str(), maxDepth);
    // filter .g files
    auto gFiles = indexer.findFilesWithSuffixes({".g"});
    int totalIndexed = gFiles.size();

    if (firstRun) {
        // first run: insert all models
        for (const auto& file : gFiles) {
            if (repo.insertModel(minimalModelData(file)))
                ++inserted;

            // progress update
            if (++handled % UPDATE_EVERY == 0) {
                // periodic stats push
                updateStats([&](JobServiceStats& st) {
                    st.modelsProcessed = totalIndexed; // total
                    st.jobsNew         = handled;
                });
            }
        }
    } else {
        // fill set for fast lookup
        std::unordered_set<std::string> scannedList;
        scannedList.reserve(gFiles.size());
        for (const auto& file : gFiles)
            scannedList.emplace(norm(file));

        // get current rows in db -> index in map for faster lookup
        const std::vector<ModelData> current = repo.getAll();
        std::unordered_map<std::string, int> pathToIdx;
        pathToIdx.reserve(current.size());
        for (int i = 0; i < current.size(); ++i) {
            pathToIdx.emplace(norm(current[i].file_path), i);
        }

        // un-include all current files
        repo.markAllNotIncluded();

        // iterate on scanned files - existing files should get is_included=true back
        //                            new files just get inserted
        for (const auto& path : scannedList) {
            auto it = pathToIdx.find(path);
            if (it != pathToIdx.end()) {
                int idx = it->second;
                if (current[idx].is_included)
                    // existing model, turn inclusion back on if it was originally
                    (void)repo.setModelIncluded(current[idx].id, true);
            } else {
                // new model; insert
                if (repo.insertModel(minimalModelData(path)))
                    ++inserted;
            }

            // progress update
            if (++handled % UPDATE_EVERY == 0) {
                // periodic stats push
                updateStats([&](JobServiceStats& st) {
                    st.modelsProcessed = totalIndexed; // total
                    st.jobsNew         = handled;
                });
            }
        }
    }

    // final update stats; send inserted count
    LOG_INFO << "[JobManager::serviceLoop] inserted " << inserted << " file(s)." << LOG_ENDL;
    updateStats([&](JobServiceStats& st) {
        st.modelsProcessed = inserted;          // models indexed
        st.jobsNew         = inserted;          // models inserted into repo
    });

    // for now this is a one-pass loop. Eventually we'll want to poll or something while(running)
}
