#include "JobWorker.h"

#include <unordered_map>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <bu/process.h>         // bu_pid()

#include "Model.h"
#include "SQLJobQueue.h"
#include "ProcessHandler.h"
#include "ThumbHandler.h"
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
    r.emplace("process", std::make_unique<ProcessHandler>(repo, queue, dataRoot, service));
    r.emplace("thumb",   std::make_unique<ThumbHandler>(dataRoot, service, repo));
    //r.emplace("dummy",   std::make_unique<DummyHandler>(dataRoot, service, repo));

    // add more directives here ...
    return r;
}

// spawn n-workerCount threads, each looping on claim -> handle -> finish. Threads will force
// stop when given stopFlag
static std::vector<std::thread> spawnWorkerThreads(const fs::path& jobsDir,
                                                   const fs::path& dataDir,
                                                   const fs::path& rootDir,
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
            SQLJobQueue queue(jobsDir);
            Model       repo(rootDir.string());

            // each thread gets its own handler registry
            auto handlers = makeHandlerRegistry(repo, queue, dataDir, service);

            // worker id using pid+threadIdx
            const std::string workerId = std::to_string(pid) + "-" + std::to_string(workerIdx);

            // jitter polling
            const int pollJitterMs = static_cast<int>((workerIdx % 7) * 17);

            // 'brains': claim and handle jobs
            while (!stopFlag.load()) {
                auto jobOpt = queue.claimJob(workerId);
                if (!jobOpt) {
                    // no job to claim; slight delay and try again
                    std::this_thread::sleep_for(std::chrono::milliseconds(200 + pollJitterMs));
                    continue;
                }

                // got a job
                auto job = *jobOpt;

                auto it = handlers.find(job.directive);
                if (it != handlers.end()) {
                    // found a valid handler
                    bool success = false;

                    // signal start
                    if (service)
                        service->directiveStarted(QString::fromStdString(job.directive),
                                                  QString::fromStdString(job.fileId));

                    try {
                        auto ret = it->second->handle(job, stopFlag);
                        success = ret.success;
                    } catch (...) {
                        // something went wrong
                        success = false;
                    }

                    // signal finish
                    if (service)
                        service->directiveFinished(QString::fromStdString(job.directive),
                                                   QString::fromStdString(job.fileId),
                                                   success);
                }

                // whether we passed or failed, finsh the job
                queue.finish(job);
            }
        });
    }

    return threads;
}

void JobWorker::serviceLoop() {
    // TODO: a lot of these should collapse into a 'Library'?
    Model            repo(rootDir());       // get unprocessed models
    SQLJobQueue      queue(jobsDir());      // queue unprocessed models
    auto             service  = static_cast<QtJobServiceBase*>(this);

    // spawn worker threads that inf. process jobs until stopFlag is true
    std::atomic<bool> stopFlag{false};
    QSettings settings;
    size_t threadCount = settings.value("jobs/numThreads", 1).toInt();
    std::cerr << "[JobWorker::ServiceLoop()] jobs/numThreads: " << threadCount << "\n";
    auto threads = spawnWorkerThreads(jobsDir(), dataDir(), rootDir(), service, threadCount, stopFlag);

    // main loop: poll -> enqueue -> drain -> repeat
    while (state() == JobServiceState::Running) {
        // poll unprocessed models
        auto unproc = repo.getIncludedNotProcessedModels();

        // enqueue "process" jobs
        for (auto const& modelData : unproc) {
            /* NOTE: for this pass, we use just the absolute filepath to enqueue a "process" job
             * since we don't have any file introspection yet. Subsequent jobs get a 
             * proper fileId which should more uniquely identify the file and link it to
             * a unique output directory
             */
            std::string dummyId = std::to_string(std::hash<std::string>{}(modelData.file_path));
            queue.createJob(dummyId, "process", modelData.file_path);
        }
        // simple stat update for number of jobs
        if (service && unproc.size()) {
            service->updateStats([&](JobServiceStats& st) {
                st.jobsNew = unproc.size();
            });
        }

        // spin while we work through job queue
        // TOOD: implement pendingCount()
        /*while (queue.pendingCount() > 0 && state() == JobServiceState::Running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }*/

        // pause before re-poll
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // TODO: probably want to signal to refresh all models periodically so other instances updates are reflected
    }

    // stopped running: signal threads to stop and join
    stopFlag.store(true);
    for (auto& t : threads) {
        if (t.joinable())
            t.join();
    }
}
