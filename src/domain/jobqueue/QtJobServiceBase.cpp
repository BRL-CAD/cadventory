#include "QtJobServiceBase.h"

#include <QMetaType>
#include <QCoreApplication>

QtJobServiceBase::QtJobServiceBase(QObject* parent) : QObject(parent) {
    qRegisterMetaType<JobServiceStats>("JobServiceStats");
    connect(&m_thread, &QThread::finished, this, &QtJobServiceBase::finished);
}

QtJobServiceBase::~QtJobServiceBase() {
    stop();
}

void QtJobServiceBase::setRootPaths(const std::string& r,
                                    const std::string& j,
                                    const std::string& d,
                                    const std::string& m) {
    m_rootDir   = r;
    m_jobsDir   = j;
    m_dataDir   = d;
    m_modelRoot = m;
}

bool QtJobServiceBase::start() {
    if (m_state != JobServiceState::Stopped) 
        return false;

    m_state = JobServiceState::Starting;
    m_uptimeTimer.start();

    // move this QObject into its own thread so slots run there
    moveToThread(&m_thread);
    connect(&m_thread, &QThread::started, this, &QtJobServiceBase::trampoline, Qt::QueuedConnection);

    m_thread.start();
    m_state = JobServiceState::Running;
    return true;
}

void QtJobServiceBase::stop() {
    if (m_state == JobServiceState::Stopped) 
        return;

    m_state = JobServiceState::Stopping;
    m_thread.quit();
    m_thread.wait();
    m_state = JobServiceState::Stopped;
}

JobServiceState QtJobServiceBase::state() const { 
    return m_state.load(); 
}

JobServiceStats QtJobServiceBase::stats() const {
    std::lock_guard<std::mutex> lock(m_statMtx);
    return m_stats;
}

void QtJobServiceBase::updateStats(const std::function<void(JobServiceStats&)>& mut) {
    {
        std::lock_guard<std::mutex> lock(m_statMtx);
        mut(m_stats);
        m_stats.uptime = std::chrono::seconds(m_uptimeTimer.elapsed() / 1000);
    }

    emit statsUpdated(m_stats);
}

void QtJobServiceBase::trampoline() {
    try {
        // runs until stop()
        serviceLoop();
    } catch (const std::exception& e) {
        emit errorOccurred(QString::fromUtf8(e.what()));
    } catch (...) {
        emit errorOccurred(QStringLiteral("Unknown exception in serviceLoop()"));
    }
}
