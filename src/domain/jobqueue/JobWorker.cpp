#include "JobWorker.h"
#include <QThread>
#include <chrono>

// TODO/FIXME: Dummy placeholders; replace with real headers
class ModelRepository{};
class FileJobQueue{};

void JobWorker::serviceLoop() {
    m_repo  = new ModelRepository;
    m_queue = new FileJobQueue;

    while (state() == JobServiceState::Running) {
        // TODO:
        // 1) Pull unprocessed models from sqlite via m_repo
        // 2) Create jobs in jobs/new via m_queue if missing
        // 3) Claim & execute job (implement a thread pool for paralleling jobs?) (need a m_directiveHandler?)

        // 4) Update m_stats
        updateStats([&](JobServiceStats& st) {
            // update st.whatever
        });

        // pause before next fs check
        QThread::msleep(100);
    }

    delete m_repo;
    delete m_queue;
}
