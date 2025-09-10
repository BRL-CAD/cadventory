#include "JobManager.h"
#include "Logger.h"

#include <filesystem>

#include "FilesystemIndexer.h"
#include "Model.h"

void JobManager::serviceLoop() {
    const std::string root = rootDir();      // inherit from QtJobServiceBase
    const long        maxDepth = 3;          // default depth

    Model repo(root);

    // scan filesystem
    FilesystemIndexer indexer(root.c_str(), maxDepth);

    // filter .g files
    auto gFiles = indexer.findFilesWithSuffixes({".g"});
    std::size_t UPDATE_EVERY = 1000;   // update stats emit every n-repo inserts

    // insert bare-bones model rows (so our workers can find them)
    std::size_t inserted = 0;   // for stats
    std::size_t scanned = 0;
    for (const auto& file : gFiles) {
        /* TODO: we'll eventually want to compare the current scan to what's in the repo
         * for a better state of new/deleted/updated files. But for now we'll just
         * assume we're starting from a fresh scan and insert everything
         */
        // consolidate_filesystem_state();

        ModelData md{};
        md.short_name   = std::filesystem::path(file).filename().string();
        md.file_path    = std::filesystem::path(file).generic_string();       // use generic_string so we always have '/' separators
        md.is_processed = false;
        md.is_included  = true;   // new files are included by default

        if (inserted % UPDATE_EVERY == 0) {
            // periodic stats push
            updateStats([&](JobServiceStats& st) {
                st.modelsProcessed = gFiles.size(); // scanned
                st.jobsNew         = scanned;
            });
        }

        if (repo.insertModel(md))
            ++inserted;

        ++scanned;
    }

    // update stats
    LOG_INFO << "[JobManager::serviceLoop] inserted " << inserted << " file(s)." << LOG_ENDL;
    updateStats([&](JobServiceStats& st) {
        st.modelsProcessed = inserted;          // models indexed
        st.jobsNew         = inserted;          // models inserted into repo
    });

    // for now this is a one-pass loop. Eventually we'll want to poll or something while(running)
}
