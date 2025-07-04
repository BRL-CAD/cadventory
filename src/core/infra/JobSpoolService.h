#pragma once
#include <QObject>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QTimer>
#include <QMutex>
#include <unordered_set>

#include "IModelRepository.h"

/* JobSpoolService
 * 
 * manages 'jobs' by original filename using atomic files in .../.cadventory/jobs/
 */
class JobSpoolService : public QObject
{
    Q_OBJECT
public:
    explicit JobSpoolService(QString libraryRoot,
                             IModelRepository& repo,
                             QObject* parent = nullptr);
    ~JobSpoolService() override;

    /* ---------------- job management ---------------- */
    // add job to .../jobs/new/
    bool    enqueueJob(const QString& filePath);
    bool    enqueueJob(const std::string& filepath);
    // take job from .../jobs/new -> /cur
    QString takeJob(QString* jobFilePath);
    // take job from .../jobs/cur -> /done
    bool    markDone(const QString& curJobPath);

    /* ---------------- maintenence ---------------- */
    // start maintenence timer which is in charge of:
    //     - syncing job progress with ModelRepo                                        (every 'tickMs' interval)
    //     - periodically cleaning up failed 'cur/' jobs and requeueing into 'new/'     (every ('tickMs' % JANITOR_EVERY_TICKS) interval)
    void    startAutoMaintenence(int tickMs = 10000);

    // force sync of in-memory cache with repo (usually is handled with startAutoMaintenence())
    void syncWithRepo();

signals:
    void jobQueued   (const QString& path);
    void jobClaimed  (const QString& path);
    void jobFinished (const QString& path);
    void progress    (quint64 done, quint64 total);

private slots:
    void maintenanceTick();                            // timer slot

private:
    /* ---------------- helpers ---------------- */
    static std::string hashPath(const QString& abs);
    static QString hashDirs(const QString& abs);
    void janitorDirs();
    void pruneEmptyDirs();

    /* ---------------- data members ---------------- */
    const QString            m_libRoot;     // plain library root
    const QString            m_jobsRoot;    // m_libRoot/JOBS_DIR
    IModelRepository&        m_repo;        // not owned; model repo

    // maintenence
    QTimer                   m_timer;
    quint32                  m_lastJanitorBucketIdx = 0;

    // in-memory progress
    QMutex                          m_mutex;
    std::unordered_set<std::string> m_doneCache;        // hashed paths
    QHash<QString,QString>          m_jobMap;           // jobFile -> origFilepath
    std::vector<QString>            m_pendingFlush;
    QDateTime                       m_lastSync;

    /* ---------------- organization ---------------- */
    static constexpr inline char JOBS_DIR[] = "/.cadventory/jobs";             // library_root/JOBS_DIR
    static constexpr inline char NEW_DIR[]  = "/new";                          // library_root/JOBS_DIR/NEW_DIR
    static constexpr inline char CUR_DIR[]  = "/cur";                          // library_root/JOBS_DIR/CUR_DIR
    static constexpr inline char DONE_DIR[] = "/done";                         // library_root/JOBS_DIR/DONE_DIR
    static constexpr int JANITOR_EVERY_TICKS = 60;                             // call janitor less frequently than sync (tickMs % JANITOR_EVERY_TICKS) => 1s -> 1min
    static constexpr qint64 CUR_JOB_TIMEOUT = 60 * 60 * 1000;                  // consider a 'cur' job failed if its been running > (60min)
    static constexpr int JANITOR_BUCKETS_PER_PASS = 256;                       /* our janitor will only check (256) buckets in 'done' in each pass 
                                                                                * 65536 / BUCKETS * (tickMs % JANITOR_EVERY_TICKS) =>
                                                                                * 65536 / 256 * (10000 % 60) = ~2days to iterate over all files in 'done'
                                                                                */
    static constexpr qint64 DONE_CLEANUP_TIME = 14LL * 24 * 60 * 60 * 1000;    // interval to let the janitor remove jobs in 'done' (14 days)
};
