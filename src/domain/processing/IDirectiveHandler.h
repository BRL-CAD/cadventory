#pragma once

#include "QtJobServiceBase.h"
#include "SQLJobQueue.h"    // needed for JobDescriptor - TODO: should that live by itself?

#include <filesystem>
#include <atomic>

class IDirectiveHandler {
public:
    virtual ~IDirectiveHandler() = default;

    // handle the given job. 'stopFlag' may be set to true to abort early
    virtual void handle(const JobDescriptor& job,
                        std::atomic<bool>& stopFlag) = 0;

protected:

    // dataDir: .cadventory/data/ directory
    // service: qt tie so we can emit start/finish signals
    IDirectiveHandler(const std::filesystem::path& dataDir,
                      QtJobServiceBase* service)
        : _dataDir(dataDir), _service(service) {}

    const std::filesystem::path _dataDir;
    QtJobServiceBase*           _service;

    // emit helpers
    void emitStart(const JobDescriptor& job) const {
        if (!_service)
          return;

        QMetaObject::invokeMethod(_service, "directiveStarted",
            Qt::QueuedConnection,
            Q_ARG(QString, QString::fromStdString(job.directive)),
            Q_ARG(QString, QString::fromStdString(job.fileId)));
    }

    void emitFinish(const JobDescriptor& job, bool ok = true) const {
        if (!_service)
          return;

        QMetaObject::invokeMethod(_service, "directiveFinished",
            Qt::QueuedConnection,
            Q_ARG(QString, QString::fromStdString(job.directive)),
            Q_ARG(QString, QString::fromStdString(job.fileId)),
            Q_ARG(bool, ok));
    }
};
