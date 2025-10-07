#include "JobWorker.h"
#include "Logger.h"

#include <unordered_map>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <bu/process.h>         // bu_pid()

#include "Model.h"
#include "HiddenDir.h"
#include "SQLJobQueue.h"
#include "ProcessHandler.h"
#include "ThumbHandler.h"
#include "GistHandler.h"
#include "DummyHandler.h"

namespace fs = std::filesystem;

/*** Helper functions ***/
using HandlerRegistry = std::unordered_map<std::string, std::unique_ptr<IDirectiveHandler>>;
// build the directive handlers. handlers get a pointer back to the QtJobServiceBase
// so they can emit progress / completion
static auto makeHandlerRegistry(Model& repo,
                                SQLJobQueue& queue,
                                const fs::path& dataRoot,
                                QtJobServiceBase* service)
{
    HandlerRegistry r;
    r.emplace("process",     std::make_unique<ProcessHandler>(repo, queue, dataRoot, service));
    r.emplace("thumb",       std::make_unique<ThumbHandler>(dataRoot, service, repo));
    r.emplace("gist_page",   std::make_unique<GistHandler>(dataRoot, service, repo, queue));
    r.emplace("gist_report", std::make_unique<GistHandler>(dataRoot, service, repo, queue));
    //r.emplace("dummy",   st d::make_unique<DummyHandler>(dataRoot, service, repo));

    // add more directives here ...
    return r;
}

// spawn n-workerCount threads, each looping on claim -> handle -> finish. Threads will force
// stop when given stopFlag
static std::vector<std::thread> spawnWorkerThreads(const HiddenDir& paths,
                                                   QtJobServiceBase* service,
                                                   size_t workerCount,
                                                   std::atomic<bool>& stopFlag)
{
    std::vector<std::thread> threads;
    threads.reserve(workerCount);

    const int pid = bu_pid();
    for (size_t workerIdx = 0; workerIdx < workerCount; ++workerIdx) {
        threads.emplace_back([=, &stopFlag]() {
            // each thread gets their own db connections
            SQLJobQueue queue(paths.jobsDir());
            Model       repo(paths.libRoot());

            // each thread gets its own handler registry
            auto handlers = makeHandlerRegistry(repo, queue, paths.dataDir(), service);

            // worker id using pid+threadIdx
            const std::string workerId = std::to_string(pid) + "-" + std::to_string(workerIdx);

            // jitter polling
            const int pollJitterMs = static_cast<int>((workerIdx % 7) * 17);

            // 'brains': claim and handle jobs
            while (!stopFlag.load()) {
                try {
                    auto jobOpt = queue.claimJob(workerId);
                    if (!jobOpt) {
                        // no job to claim; slight delay and try again
                        std::this_thread::sleep_for(std::chrono::milliseconds(200 + pollJitterMs));
                        continue;
                    }

                    // got a job
                    auto job = *jobOpt;
                    bool success = false;

                    // signal start
                    if (service)
                        service->directiveStarted(QString::fromStdString(job.directive),
                                                  QString::fromStdString(job.fileId));

                    // lazy: try will catch if we dont have a handler, or if something goes wrong within it
                    HandlerResult result;
                    try {
                        result = handlers.at(job.directive)->handle(job, stopFlag);
                        success = result.success;
                    } catch (...) {
                        success = false;
                    }

                    // signal finish
                    if (service)
                        service->directiveFinished(QString::fromStdString(job.directive),
                                                   QString::fromStdString(job.fileId),
                                                   success);

                    // whether we passed or failed, finsh the job
                    queue.finish(job);

                    // do we need to re-queue?
                    if (!success && result.message == "requeue")
                        queue.createJob(job.fileId, job.directive, job.sourcePath);
                } catch (const std::exception&) {
                    // something probably went awry with jobqueue claim/finish. Don't kill the thread
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }
        });
    }

    return threads;
}

struct safeThreadExit {
    // RAII
    std::vector<std::thread>& threads;
    std::atomic<bool>& stopFlag;

    ~safeThreadExit() {
        // signal threads to stop and join
        stopFlag.store(true);
        for (auto& thread : threads) {
            if (thread.joinable())
                thread.join();
        }
    }
};

void JobWorker::serviceLoop() {
    // TODO: a lot of these should collapse into a 'Library'?
    Model            repo(paths().libRoot());       // get unprocessed models
    SQLJobQueue      queue(paths().jobsDir());      // queue unprocessed models
    auto             service  = static_cast<QtJobServiceBase*>(this);

    // spawn worker threads that inf. process jobs until stopFlag is true
    std::atomic<bool> stopFlag{false};
    QSettings settings;
    size_t threadCount = settings.value("jobs/numThreads", 0).toInt();
    LOG_DEBUG << "[JobWorker::ServiceLoop()] jobs/numThreads: " << threadCount << LOG_ENDL;
    auto threads = spawnWorkerThreads(paths(), service, threadCount, stopFlag);
    safeThreadExit _guard{threads, stopFlag};

    // time-keeping
    using clock = std::chrono::steady_clock;
    const auto refreshInterval = std::chrono::seconds(5);   // refresh job count
    const auto scanInterval = std::chrono::seconds(15);     // scan model repo db
    const auto idleDelay = std::chrono::milliseconds(200);

    // force initial checks
    size_t job_cnt = 0;
    auto lastRefresh = clock::now() - refreshInterval;
    auto lastScan = clock::now() - scanInterval;

    // main loop: drain -> enqueue -> repeat
    while (state() == JobServiceState::Running) {
        try {
            const auto now = clock::now();

            // refresh job count periodically (emit to service if we have one)
            if (now - lastRefresh >= refreshInterval) {
                lastRefresh = now;
                job_cnt = queue.totalCount();

                if (service) {
                    service->emitRefreshSuggested();
                    service->updateStats([&](JobServiceStats& st) {
                        st.jobsNew = job_cnt;
                    });
                }
            }

            // spin while we still have jobs
            if (job_cnt > 0) {
                // wait for a refresh
                std::this_thread::sleep_for(refreshInterval);
                continue;
            }

            // no queued work found: consider scalling the repo
            if (threadCount && now - lastScan >= scanInterval) {
                lastScan = now;

                // fetch unprocessed models
                auto unproc = repo.getIncludedNotProcessedModels();

                // enqueue "process" jobs
                for (auto const& modelData : unproc) {
                    /* NOTE: for this pass, we use just the filepath to enqueue a "process" job
                     * since we don't have any file introspection yet. Subsequent jobs get a 
                     * proper fileId which should more uniquely identify the file contents
                     */
                    queue.createJob(modelData.file_path, "process", modelData.file_path);
                }

                // assume we queued jobs
                job_cnt = unproc.size();
            }
        } catch (const std::exception&) {
            // ignore any errors thrown so loop always stays alive
        }

        // light idle delay to avoid tight loops
        std::this_thread::sleep_for(idleDelay);
    }
}
