// ProcessHandler.h

#pragma once

#include <atomic>
#include <filesystem>

#include "IDirectiveHandler.h"
#include "Model.h"
#include "SQLJobQueue.h"
#include "ProcessGFiles.h"

class ProcessHandler : public IDirectiveHandler {
public:
    ProcessHandler(Model& repo,
                   SQLJobQueue& queue,
                   const std::filesystem::path& dataDir,
                   QtJobServiceBase* service)
        : IDirectiveHandler(dataDir, service, repo)
        , m_queue(queue)
    {}

    HandlerResult handle(const JobDescriptor& job,
                std::atomic<bool>& /*stopFlag*/) override
    {
        // check this job is still needed
        auto ctx = needsHandled(job, "");
        if (!ctx)
            return {true, "no work to do"};

        // signal start
        emitStart(job);

        // select file processor (for now assume we just have .g)
        if (std::filesystem::path(job.sourcePath).extension() != ".g") {
            emitFinish(job, false);
            return {false, "no available file processor"};
        }
        ProcessGFiles processor(&_repo);

        auto ret = processor.processGFile(*ctx->modeldata.get());
        if (!ret) {
            // something went wrong
            emitFinish(job, false);
            return {false, "processor.processGFile() error"};
        }
        ModelData final = *ret;

        // queue all follow-up job directives needed by the processor
        for (auto& directive : processor.getAllNeededDirectives()) {
            m_queue.createJob(final.is_processed_dir, directive, job.sourcePath);
        }

        // signal finished
        emitFinish(job);
        return {true, ""};
    }

private:
    SQLJobQueue& m_queue;
};
