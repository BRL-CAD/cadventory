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

    void handle(const JobDescriptor& job,
                std::atomic<bool>& /*stopFlag*/) override
    {
        // signal start
        emitStart(job);

        // select file processor (for now assume we just have .g)
        if (std::filesystem::path(job.sourcePath).extension() != ".g") {
            emitFinish(job, false);
            return;
        }
        ProcessGFiles processor(&_repo);

        // verify we still have an entry for this filepath in the ModelRepo
        ModelData existing = _repo.getModelByFilePath(job.sourcePath);

        if (!existing.id) {
            // this shouldn't be possible if our worker+manager are working properly
            emitFinish(job, false);
            return;
        }

        auto ret = processor.processGFile(existing);
        if (!ret) {
            // something went wrong
            emitFinish(job, false);
            return;
        }
        ModelData final = *ret;

        // queue all follow-up job directives needed by the processor
        for (auto& directive : processor.getAllNeededDirectives()) {
            m_queue.createJob(final.is_processed_dir, directive, job.sourcePath);
        }

        // signal finished
        emitFinish(job);
    }

private:
    SQLJobQueue& m_queue;
};
