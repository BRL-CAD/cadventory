#pragma once
#include <QObject>
#include <QThread>
#include <QElapsedTimer>
#include <atomic>
#include <mutex>
#include <functional>

#include "IJobService.h"

/* manages all qt specifics for a job service - derived classes can/should be qt free */
class QtJobServiceBase : public QObject, public IJobService
{
    Q_OBJECT
public:
    explicit QtJobServiceBase(QObject* parent=nullptr);
    ~QtJobServiceBase() override;

    // IJobService interface overrides
    void setRootPaths(const std::string& jobsDir,
                      const std::string& dataDir,
                      const std::string& modelRoot) override;

    bool start() override;   // spawns m_thread and runs serviceLoop()
    void stop()  override;

    JobServiceState state() const override;
    JobServiceStats stats() const override;

signals:
    void statsUpdated(JobServiceStats);
    void finished();                         // emitted after stop() completes
    void errorOccurred(QString message);     // emitted on unhandled exception

protected:
    // 'brains' of derived classes - runs inside m_thread
    virtual void serviceLoop() = 0;

    // helper for derived classes to update statistics safely and emit
    void updateStats(const std::function<void(JobServiceStats&)>& mutator);

    // access to configuration paths for derived classes
    const std::string& jobsDir()   const { return m_jobsDir;   }
    const std::string& dataDir()   const { return m_dataDir;   }
    const std::string& modelRoot() const { return m_modelRoot; }

private slots:
    void trampoline();                // calls serviceLoop()

private:
    // configuration paths
    std::string m_jobsDir;
    std::string m_dataDir;
    std::string m_modelRoot;

    // runtime
    QThread m_thread;
    mutable std::mutex m_statMtx;
    QElapsedTimer m_uptimeTimer;

    // state
    std::atomic<JobServiceState> m_state { JobServiceState::Stopped };
    JobServiceStats m_stats;
};
