#include "JobWorker.h"

#include <unordered_map>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>

#include "Model.h"
#include "SQLJobQueue.h"
#include "ProcessHandler.h"
#include "DummyHandler.h"


/*** Helper functions ***/
using HandlerRegistry = std::unordered_map<std::string, std::unique_ptr<IDirectiveHandler>>;
// build the directive handlers. handlers get a pointer back to the QtJobServiceBase
// so they can emit progress / completion
static auto makeHandlerRegistry(Model& repo,
                                SQLJobQueue& queue,
                                const std::filesystem::path& dataRoot,
                                QtJobServiceBase* service)
{
    HandlerRegistry r;
    r.emplace("process", std::make_unique<ProcessHandler>(repo, queue, dataRoot, service));
    r.emplace("dummy", std::make_unique<DummyHandler>(dataRoot, service));

    // add more directives here ...
    return r;
}

// spawn n-workerCount threads, each looping on claim -> handle -> finish. Threads will force
// stop when given stopFlag
static auto spawnWorkerThreads(SQLJobQueue& queue,
                               const std::filesystem::path& dataRoot,
                               HandlerRegistry& handlers,
                               QtJobServiceBase* service,
                               size_t workerCount,
                               std::atomic<bool>& stopFlag)
{
    std::vector<std::thread> threads;
    threads.reserve(workerCount);

    for (size_t i = 0; i < workerCount; ++i) {
        threads.emplace_back([&, i]() {
            // TODO: better specific workerId (hostname?)
            int workerId = static_cast<int>(i);
            std::string workerId_str = std::to_string(workerId);
            while (!stopFlag.load()) {
                auto jobOpt = queue.claimJob(workerId_str);
                if (!jobOpt) {
                    // no job to claim; slight delay and try again
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    continue;
                }

                // got a job
                auto job = *jobOpt;

                auto it = handlers.find(job.directive);
                if (it != handlers.end()) {
                    // found a valid handler
                    bool success = false;

                    // signal start
                    service->directiveStarted(QString::fromStdString(job.directive), 
                                              QString::fromStdString(job.fileId));
                    try {
                        it->second->handle(job, stopFlag);
                        success = true;
                    } catch (...) {
                        // something went wrong
                        success = false;
                    }

                    // signal finish
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
    Model            repo(modelRoot());     // just using this for SQL repo interactions
    SQLJobQueue      queue(jobsDir());
    auto             dataRoot = dataDir();
    auto             service  = static_cast<QtJobServiceBase*>(this);

    // build handlers (with access to the service for signals)
    auto handlers   = makeHandlerRegistry(repo, queue, dataRoot, service);

    // spawn worker threads that inf. process jobs until stopFlag is true
    std::atomic<bool> stopFlag{false};
    // TODO: make threadCount a config option
    size_t threadCount = 1;
    auto threads = spawnWorkerThreads(queue, dataRoot, handlers, service, threadCount, stopFlag);

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
        service->updateStats([&](JobServiceStats& st) {
            st.jobsNew = unproc.size();
        });

        // spin while we work through job queue
        // TOOD: implement pendingCount()
        /*while (queue.pendingCount() > 0 && state() == JobServiceState::Running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }*/

        // pause before re-poll
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // stopped running: signal threads to stop and join
    stopFlag.store(true);
    for (auto& t : threads) {
        if (t.joinable())
            t.join();
    }
}