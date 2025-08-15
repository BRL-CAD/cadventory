#pragma once
#include "IDirectiveHandler.h"

#include <fstream>
#include <filesystem>
#include <thread>
#include <atomic>

/*** Creating Directive Handler flow
 *
 * 1) Create the handler class (this file)
 *      subclass IDirectiveHandler and implement handle()
 *
 * 2) Register it with the worker
 *      In JobWorker.cpp/JobManager.cpp -> makeHandlerRegistry()
 *          (the key string MUST match the directive queued in step 3)
 *
 * 3) Make sure jobs can be queued
 *      If this directive is part of the standard pipeline, add its name to
 *          ProcessHandler processor::getAllNeededDirectives()
 *      Otherwise, add it where necessary
 *
 * 4) Emit signals
 *      A) simple/lightweight can use the generic
            emitStart() / emitFinish() protected functions
 *      B) handler-specific UI need to add signal to QtJobServiceBase
 *          (e.g. void dummyFinished(QString fileId, bool ok);) and fire
 *          with QMetaObject::invokeMethod(_service, ... Qt::QueuedConnection)
 *
 * 5) Hook into the UI
 *      In the controller/window/widget, connect() with either the generic
 *          handler (filtering on the string), OR the new handler-specific signal
 *          from step 4b
 */

class DummyHandler : public IDirectiveHandler {
public:
    DummyHandler(const std::filesystem::path& dataDir,
                 QtJobServiceBase* service,
                 Model& repo)
        : IDirectiveHandler(dataDir, service, repo) {}

    HandlerResult handle(const JobDescriptor& job,
                std::atomic<bool>& /*stopFlag*/) override
    {
        /* NOTE: example usage of handler context
        auto ctx = needsHandled(job, "");
        if (!ctx) {
            emitFinish(job, false);
            return {true, "no work to do"};
        }
        */

        // 'work'
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // write an output file
        std::filesystem::path outputDir = _dataDir / job.fileId;
        std::filesystem::create_directories(outputDir);
        std::ofstream(outputDir / "dummy").close();
        // NOTE: ideally we'd use ctx.outputPath but this is just an example handler
        // std::ofstream(ctx.outputPath).close();

        // signal finish
        if (_service) {
            // specific 'dummyFinished'
            QMetaObject::invokeMethod(_service, "dummyFinished", Qt::QueuedConnection,
                                Q_ARG(QString, QString::fromStdString(job.fileId)),
                                Q_ARG(bool, true));
        }
        return {true, ""};
    }
};
