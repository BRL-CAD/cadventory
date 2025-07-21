#include "JobManager.h"

#include "FilesystemIndexer.h"
#include "Model.h"

void JobManager::serviceLoop() {
    const std::string root = rootDir();      // inherit from QtJobServiceBase
    const long        maxDepth = 3;            // default depth

    Model repo(root);

    // scan filesystem
    FilesystemIndexer indexer(root.c_str(), maxDepth);
    size_t totalIndexed = indexer.indexDirectory(root, maxDepth);

    // filter .g files
    auto gFiles = indexer.findFilesWithSuffixes({".g"});

    // insert bare-bones model rows (so our workers can find them)
    std::size_t inserted = 0;   // for stats
    for (const auto& file : gFiles) {
        /* TODO: we'll eventually want to compare the current scan to what's in the repo
         * for a better state of new/deleted/updated files. But for now we'll just
         * assume we're starting from a fresh scan and insert everything
         */
        // consolidate_filesystem_state();

        ModelData md{};
        md.file_path      = file;
        md.is_processed   = false;
        md.is_included    = true;   // new files are included by default

        if (repo.insertModel(md))
            ++inserted;
    }

    // update stats
    updateStats([&](JobServiceStats& st) {
        st.modelsProcessed = totalIndexed;      // models indexed
        st.jobsNew         = inserted;          // models inserted into repo
    });

    // for now this is a one-pass loop. Eventually we'll want to poll or something while(running)
}
