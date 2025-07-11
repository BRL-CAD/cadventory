#include "JobManager.h"
#include <QThread>
#include <chrono>

// TODO/FIXME: Dummy placeholders; add in the real headers
class FilesystemIndexer{};
class ModelRepository{};

void JobManager::serviceLoop() {
    m_indexer = new FilesystemIndexer;
    m_repo    = new ModelRepository;

    while (state() == JobServiceState::Running)
    {
        // TODO: FilesystemIndexer scan, sync with sqlite,
        //       janitor stale jobs, mark models complete

        updateStats([&](JobServiceStats& st){
            // update st.whatever
        });

        // let fs settle before next scan
        QThread::msleep(100);
    }

    delete m_indexer;
    delete m_repo;
}
