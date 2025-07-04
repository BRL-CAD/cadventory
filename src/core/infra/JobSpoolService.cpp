#include "JobSpoolService.h"

#include <QDirIterator>
#include <QFile>
#include <QRandomGenerator>
#include <QDateTime>

JobSpoolService::JobSpoolService(QString libRoot, IModelRepository& repo, QObject* parent) 
    : QObject(parent),
      m_libRoot(QDir(libRoot).canonicalPath()),
      m_jobsRoot(m_libRoot + JOBS_DIR),
      m_repo(repo),
      m_lastSync(QDateTime::currentDateTime())
{
    // make job status directories
    QDir().mkpath(m_jobsRoot + NEW_DIR);
    QDir().mkpath(m_jobsRoot + CUR_DIR);
    QDir().mkpath(m_jobsRoot + DONE_DIR);

    // preload done cache if we already have done jobs
    QDirIterator it(m_jobsRoot + DONE_DIR, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString file = it.next();
        m_doneCache.insert(hashPath(QFileInfo(file).completeBaseName()));
    }
}

JobSpoolService::~JobSpoolService() {
    m_timer.stop();

    // sync our progress
    syncWithRepo();

    // cleanup what we can
    janitorDirs();
    pruneEmptyDirs();
}

bool JobSpoolService::enqueueJob(const QString& absFilename) {
    const std::string key = hashPath(absFilename);
    {
        QMutexLocker lock(&m_mutex);
        if (m_doneCache.count(key)) 
            // already done
            return false;
    }

    const QString jp = m_jobsRoot + NEW_DIR + '/' + hashDirs(absFilename) + '/' + QFileInfo(absFilename).fileName() + ".job";
    if (QFile::exists(jp)) 
        // already queueud
        return false;

    // create .job file from original filename
    QDir().mkpath(QFileInfo(jp).path());
    QFile f(jp);
    if (!f.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        // raced (and lost)
        return false;

    // write our original path to the file
    f.write(absFilename.toUtf8());
    f.close();

    // map our job to its absolute name
    {
        QMutexLocker lock(&m_mutex);
        m_jobMap[jp] = absFilename;         // fast map to avoid a few file reads for orig filepath
    }

    // successfully queued new job
    emit jobQueued(absFilename);
    emit progress(m_doneCache.size(), m_doneCache.size() + 1);
    return true;
}

bool JobSpoolService::enqueueJob(const std::string& filepath) {
    return enqueueJob(QString::fromStdString(filepath));
}

QString JobSpoolService::takeJob(QString* jobOut) {
    QDirIterator it(m_jobsRoot + NEW_DIR, QDir::Files, QDirIterator::Subdirectories);

    // iterate 'new' directory, find a job candidate
    while (it.hasNext()) {
        //  get the path associated with this job
        const QString job = it.next();                  // aa/bb/file.job
        QString origFilepath;                           // library/file.g

        {
            // get our original filepath
            QMutexLocker lock(&m_mutex);
            origFilepath = m_jobMap.value(job);
        }

        if (origFilepath.isEmpty()) {
            // this instance didn't enqueue it - read the first line of the file
            QFile read(job);
            if (!read.open(QIODevice::ReadOnly)) {
                //QFile::remove(job);     // unreadable - discard
                continue;
            }
            origFilepath = QString::fromUtf8(read.readAll()).trimmed();
            if (origFilepath.isEmpty()) {
                //QFile::remove(job);     // corrupt job might as well delete
                continue;
            }
        }

        //  skip if already done in cache
        if (m_doneCache.count(hashPath(origFilepath))) {
            // job is already handled but wasn't remove from 'new' directory, remove file
            QFile::remove(job);
            continue;
        }

        // skip if already done in 'done' dir
        QString doneSibling = job;
        doneSibling.replace(NEW_DIR, DONE_DIR);
        QDir doneDir(QFileInfo(doneSibling).absolutePath());
        if (!doneDir.entryList({ QFileInfo(job).fileName() + ".*" }, QDir::Files).isEmpty()) {
            QFile::remove(job);
            continue;
        }

        //  claim the job: new -> cur
        QString rel = job.mid((m_jobsRoot + NEW_DIR).size());
        QString dst = m_jobsRoot + CUR_DIR + rel + '.' + QString::number(QRandomGenerator::global()->generate64(), 36);

        QDir().mkpath(QFileInfo(dst).path());
        if (!QFile::rename(job, dst)) {
            // raced (and lost) - make sure we dont spin on this path again
            QMutexLocker lock(&m_mutex);
            m_jobMap.remove(job);
            continue;
        }

        // update jobMap with new key
        {
            QMutexLocker lock(&m_mutex);
            m_jobMap.remove(job);
            m_jobMap[dst] = origFilepath;
        }

        if (jobOut) 
            *jobOut = dst;
        emit jobClaimed(origFilepath);
        // success
        return origFilepath;
    }

    // no work found
    return {};
}

bool JobSpoolService::markDone(const QString& curJob) {
    // get original file path from jobMap
    QString origFilepath;
    {
        // get original file path
        QMutexLocker lock(&m_mutex);
        auto it = m_jobMap.find(curJob);
        if (it == m_jobMap.end())
            // we dont have a mapping to original filepath; can't mark done
            return false;
        origFilepath = it.value();
        m_jobMap.erase(it);
    }

    // move from 'cur' -> 'done'
    QString dst = curJob;
    dst.replace(CUR_DIR, DONE_DIR);
    QDir().mkpath(QFileInfo(dst).path());
    if (!QFile::rename(curJob, dst))
        // raced (and lost); someone else already marked it
        return false;

    // cache to batch feed repo on next sync
    const std::string key = hashPath(origFilepath);
    {
        QMutexLocker lock(&m_mutex);
        m_doneCache.insert(key);
        m_pendingFlush.push_back(origFilepath);
    }

    emit jobFinished(origFilepath);

    // estimate remaining with size of 'new' dir
    quint64 newRem = QDir(m_jobsRoot + NEW_DIR).entryList(QDir::Files | QDir::NoDotAndDotDot).size();
    emit progress(m_doneCache.size(), m_doneCache.size() + newRem);
    return true;
}

void JobSpoolService::startAutoMaintenence(int tickMs) {
    connect(&m_timer, &QTimer::timeout, this,&JobSpoolService::maintenanceTick);

    m_timer.start(tickMs);
}

void JobSpoolService::maintenanceTick()
{
    static int tick = 0;
    ++tick;
    syncWithRepo();

    if (tick % JANITOR_EVERY_TICKS == 0)
        janitorDirs();
}

void JobSpoolService::syncWithRepo()
{
    // push pending
    {
        QMutexLocker lock(&m_mutex);
        if (!m_pendingFlush.empty()) {
            m_repo.markDoneBatch(m_pendingFlush);
            m_pendingFlush.clear();
        }
    }
    
    // pull latest repo status
    auto done = m_repo.fetchPendingBatch(/*m_lastSync*/);
    if (!done.empty()) {
        QMutexLocker lock(&m_mutex);
        for (const auto& p : done)
            m_doneCache.insert(hashPath(p));
    }
    m_lastSync = QDateTime::currentDateTimeUtc();
}

void JobSpoolService::janitorDirs() {
    const auto   now   = QDateTime::currentDateTimeUtc();

    // recycle timedout 'cur' jobs back to 'new'
    QDirIterator it(m_jobsRoot + CUR_DIR, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QFileInfo fi = it.fileInfo();
        // check if job file has been in 'cur' too long
        if (fi.lastModified().msecsTo(now) > CUR_JOB_TIMEOUT) {
            // move back to 'new' so someone else can try the job
            QString rel = fi.filePath().mid((m_jobsRoot + CUR_DIR).size());
            QString dst = m_jobsRoot + NEW_DIR + rel.section('.',0,0);
            QDir().mkpath(QFileInfo(dst).path());
            QFile::rename(fi.filePath(), dst);
        }
    }

    // periodically cleanup 'done' dir
    const QString doneRoot = m_jobsRoot + DONE_DIR;
    for (int i = 0; i < JANITOR_BUCKETS_PER_PASS; ++i) {
        // compute two-level hex bucket from m_lastJanitorBucketIdx
        uint hi = (m_lastJanitorBucketIdx >> 8) & 0xFF;
        uint lo = m_lastJanitorBucketIdx & 0xFF;
        QString bucketPath = QString("%1/%2/%3")
                             .arg(doneRoot)
                             .arg(QString::number(hi,16).rightJustified(2,'0'))
                             .arg(QString::number(lo,16).rightJustified(2,'0'));

        // delete old job files in this bucket
        QDirIterator fit(bucketPath, QDir::Files, QDirIterator::NoIteratorFlags);
        while (fit.hasNext()) {
            QFileInfo dfi(fit.next());
            if (dfi.lastModified().msecsTo(now) > DONE_CLEANUP_TIME) {
                QFile::remove(dfi.filePath());
            }
        }

        // rmdir if now empty
        QDir(bucketPath).rmdir(".");

        // advance to next bucket (wrap at 0xFFFF)
        m_lastJanitorBucketIdx = (m_lastJanitorBucketIdx + 1) & 0xFFFF;
    }
}

void JobSpoolService::pruneEmptyDirs() {
    QDirIterator it(m_jobsRoot, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);

    QList<QString> dirs;
    while (it.hasNext()) { 
        dirs.prepend(it.next()); 
    }

    for (const QString& d : dirs)
        QDir(d).rmdir(".");                 // succeeds only if empty
}

// static helpers
std::string JobSpoolService::hashPath(const QString& abs) {
    QByteArray h = QCryptographicHash::hash(abs.toUtf8(), QCryptographicHash::Sha256);

    // 64-hex chars
    return h.toHex().toStdString();
}

QString JobSpoolService::hashDirs(const QString& abs) {
    QByteArray md = QCryptographicHash::hash(abs.toUtf8(), QCryptographicHash::Md5);

    return QString("%1/%2")
           .arg(QString::fromLatin1(md.mid(0,1).toHex()))
           .arg(QString::fromLatin1(md.mid(1,1).toHex()));  // 256 buckets
}