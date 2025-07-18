#pragma once
#include "IDirectiveHandler.h"

#include <fstream>
#include <filesystem>
#include <thread>
#include <atomic>

class DummyHandler : public IDirectiveHandler {
public:
    DummyHandler(const std::filesystem::path& dataDir,
                 QtJobServiceBase* service)
        : IDirectiveHandler(dataDir, service) {}

    void handle(const JobDescriptor& job,
                std::atomic<bool>& /*stopFlag*/) override
    {
        // signal start
        if (_service)
            _service->directiveStarted("dummy", QString::fromStdString(job.fileId));

        // simulate 'work'
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // write an output file
        std::filesystem::path outputDir = _dataDir / job.fileId;
        std::filesystem::create_directories(outputDir);
        std::ofstream(outputDir / "dummy").close();

        // signal finish
        if (_service)
            _service->directiveFinished("dummy", QString::fromStdString(job.fileId), true);
    }
};
