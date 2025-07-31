#pragma once

#include "QtJobServiceBase.h"
#include "SQLJobQueue.h"    // needed for JobDescriptor - TODO: should that live by itself?
#include "Model.h"

#include <filesystem>
#include <atomic>
#include <memory>
#include <optional>

// bundle of useful information from the model / job to be used by the handler
struct HandlerContext {
    std::filesystem::path outputPath;       // .cadventory/data/ab/abc123/objName/directive.ext
    std::string primaryObject;
    std::shared_ptr<ModelData> modeldata;
};

class IDirectiveHandler {
public:
    virtual ~IDirectiveHandler() = default;

    // handle the given job. 'stopFlag' may be set to true to abort early
    virtual void handle(const JobDescriptor& job, std::atomic<bool>& stopFlag) = 0;

protected:

    // dataDir: .cadventory/data/ directory
    // service: qt tie so we can emit start/finish signals
    // repo:    model repo
    IDirectiveHandler(const std::filesystem::path& dataDir, QtJobServiceBase* service, Model& repo)
        : _dataDir(dataDir), _service(service), _repo(repo) {}

    // return std::nullopt if job is already finished or no longer needed
    std::optional<HandlerContext> needsHandled(const JobDescriptor& job, std::string ext);

    // emit helpers
    void emitStart(const JobDescriptor& job) const;
    void emitFinish(const JobDescriptor& job, bool ok = true) const;

    // members
    const std::filesystem::path _dataDir;
    QtJobServiceBase*           _service;
    Model&                      _repo;
};
