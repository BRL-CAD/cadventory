#pragma once
#include <QObject>
#include <QThread>
#include <QElapsedTimer>
#include <atomic>
#include <mutex>
#include <functional>

#include "IJobService.h"
#include "HiddenDir.h"

/* manages all qt specifics for a job service - derived classes can/should be qt free */
class QtJobServiceBase : public QObject, public IJobService
{
    Q_OBJECT
public:
    explicit QtJobServiceBase(QObject* parent=nullptr);
    explicit QtJobServiceBase(const QString& path, QObject* parent = nullptr);
    ~QtJobServiceBase() override;

    // IJobService interface overrides
    void setRootPath(const std::string& path) override;

    bool start() override;   // spawns m_thread and runs serviceLoop()
    void stop()  override;

    JobServiceState state() const override;
    JobServiceStats stats() const override;

    // helper for derived classes to update statistics safely and emit
    void updateStats(const std::function<void(JobServiceStats&)>& mutator);
signals:
    // general
    void statsUpdated(JobServiceStats);
    void errorOccurred(QString message);     // emitted on unhandled exception
    void finished();                         // emitted after stop() completes
    void refreshSuggested();
    // directive specific
    void directiveStarted(const QString& directive, const QString& fileId);
    void directiveFinished(const QString& directive, const QString& fileId, bool success);
public slots:
    void emitRefreshSuggested() { emit refreshSuggested(); }

protected:
    // 'brains' of derived classes - runs inside m_thread
    virtual void serviceLoop() = 0;

    // access to configuration paths for derived classes
    const HiddenDir& paths() const { return m_paths; }

private slots:
    void trampoline();                // calls serviceLoop()
    // internal slot to catch directive signals and update stats
    void onDirectiveStarted(const QString& directive, const QString& fileId);
    void onDirectiveFinished(const QString& directive, const QString& fileId, bool success);

private:
    // configuration paths
    HiddenDir m_paths;

    // runtime
    QThread m_thread;
    mutable std::mutex m_statMtx;
    QElapsedTimer m_uptimeTimer;

    // state
    std::atomic<JobServiceState> m_state { JobServiceState::Stopped };
    JobServiceStats m_stats;
};
