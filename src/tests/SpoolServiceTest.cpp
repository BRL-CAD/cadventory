// SpoolServiceTest.cpp
#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QThread>
#include <QSignalSpy>
#include <QTest>
#include <thread>
#include <chrono>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <utime.h>
#endif

#include "DirectoryScanner.h"
#include "JobSpoolService.h"
#include "ModelTestFixture.h"

const std::string TEST_NAME = "cadventory_SSTest";

// minimal in-memory repository stub
class DummyRepo : public IModelRepository
{
public:
    void markDoneBatch(const std::vector<QString> &paths) override { 
        flushed += paths.size();
    }

    std::vector<QString> fetchPendingBatch() override {
        return {};
    }

    std::size_t flushed = 0;
};

// minimal worker that claims & finishes jobs
class Worker : public QObject
{
    Q_OBJECT
public:
    explicit Worker(JobSpoolService &s) : spool(s) {}

public slots:
    void run()
    {
        QString jobFile;
        while (true) {
            const QString cad = spool.takeJob(&jobFile);
            if (cad.isEmpty()) break;           // queue empty
            QThread::msleep(100);               // .. fake processing
            spool.markDone(jobFile);
        }
        emit finished();
    }

signals:
    void finished();

private:
    JobSpoolService &spool;
};
#include "SpoolServiceTest.moc"

// helper: create dummy CAD files
static QString makeTestLibrary(std::filesystem::path tempDir, int num_files)
{
    REQUIRE(std::filesystem::exists(tempDir));
    QDir dir(tempDir);
    for (int i = 0; i < num_files; ++i) {
        QFile f(dir.path() + QString("/part_%1.g").arg(i, 3, 10, QChar('0')));
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("dummy");
    }
    return dir.path();
}


TEST_CASE("Spool queue processe workers", "[spool]")
{
    ModelTestFixture fixture(TEST_NAME);
    static int argc = 0; static char* argv[] = { nullptr };
    QCoreApplication app(argc, argv);

    // init services
    DummyRepo repo;
    JobSpoolService spool(QString::fromStdString(fixture.tempDir.string()), repo);
    DirectoryScanner scanner(&spool);

    // setup dummy files
    const int FILES = 100;
    const int TIMEOUT = 30000;
    const QString root = makeTestLibrary(fixture.tempDir, FILES);

    // queue our initial batch
    auto stats = scanner.scan(root.toStdString(), {".g"});
    REQUIRE(stats.queued == FILES);

    SECTION("Process files, 1 instance single-threaded") {
        // process using one spool
        QString jobFile;
        while (true) {
            QString filename = spool.takeJob(&jobFile);
            if (filename.isEmpty())
                break;
            REQUIRE(spool.markDone(jobFile));
        }

        // verify 'new' and 'cur' are empty; 'done' is full
        const QDir newD(QString::fromStdString(fixture.tempDir.string()) + "/.cadventory/jobs/new");
        const QDir curD(QString::fromStdString(fixture.tempDir.string()) + "/.cadventory/jobs/cur");
        const QDir doneD(QString::fromStdString(fixture.tempDir.string()) + "/.cadventory/jobs/done");
        DirectoryScanner counter;   // scanner w/o spool is just a counter
        REQUIRE(counter.scan(newD.canonicalPath().toStdString()).scanned == 0);
        REQUIRE(counter.scan(curD.canonicalPath().toStdString()).scanned == 0);
        REQUIRE(counter.scan(doneD.canonicalPath().toStdString()).scanned == FILES);

        // force sync
        spool.syncWithRepo();

        // verify 'repo' saw the files
        REQUIRE(repo.flushed == FILES);
    }

    SECTION("Process files, 1 instance multi-threaded") {
        // two workers on same spool
        QThread t1, t2;
        Worker w1(spool), w2(spool);
        w1.moveToThread(&t1);
        w2.moveToThread(&t2);

        QObject::connect(&t1, &QThread::started, &w1, &Worker::run);
        QObject::connect(&t2, &QThread::started, &w2, &Worker::run);
        QObject::connect(&w1, &Worker::finished, &t1, &QThread::quit);
        QObject::connect(&w2, &Worker::finished, &t2, &QThread::quit);

        QSignalSpy spy1(&w1, &Worker::finished), spy2(&w2, &Worker::finished);
        t1.start(); t2.start();

        // Wait up to 5 s for both workers to finish
        QElapsedTimer timer; timer.start();
        while (spy1.count() < 1 || spy2.count() < 1) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            if (timer.elapsed() > TIMEOUT)
                FAIL("timeout waiting for workers");
        }

        t1.wait(); t2.wait();

        // force sync
        spool.syncWithRepo();

        // verify 'new' and 'cur' are empty; 'done' is full
        const QDir newD(QString::fromStdString(fixture.tempDir.string()) + "/.cadventory/jobs/new");
        const QDir curD(QString::fromStdString(fixture.tempDir.string()) + "/.cadventory/jobs/cur");
        const QDir doneD(QString::fromStdString(fixture.tempDir.string()) + "/.cadventory/jobs/done");
        DirectoryScanner counter;   // scanner w/o spool is just a counter
        REQUIRE(counter.scan(newD.canonicalPath().toStdString()).scanned == 0);
        REQUIRE(counter.scan(curD.canonicalPath().toStdString()).scanned == 0);
        REQUIRE(counter.scan(doneD.canonicalPath().toStdString()).scanned == FILES);

        // verify 'repo' saw the files
        REQUIRE(repo.flushed == FILES);
    }

    SECTION("Process files, 2 instances concurrently") {
        // second service shares same disk state
        DummyRepo repo2;
        JobSpoolService spool2(root, repo2);

        QThread t1, t2;
        Worker w1(spool), w2(spool2);
        w1.moveToThread(&t1);
        w2.moveToThread(&t2);

        QObject::connect(&t1, &QThread::started, &w1, &Worker::run);
        QObject::connect(&t2, &QThread::started, &w2, &Worker::run);
        QObject::connect(&w1, &Worker::finished, &t1, &QThread::quit);
        QObject::connect(&w2, &Worker::finished, &t2, &QThread::quit);

        QSignalSpy spy1(&w1, &Worker::finished), spy2(&w2, &Worker::finished);
        t1.start(); t2.start();

        // Wait up to 5 s for both workers to finish
        QElapsedTimer timer; timer.start();
        while (spy1.count() < 1 || spy2.count() < 1) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            if (timer.elapsed() > TIMEOUT) {
                t1.wait(); t2.wait();
                FAIL("timeout waiting for workers");
            }
        }

        t1.wait(); t2.wait();

        // verify 'new' and 'cur' are empty; 'done' is full
        const QDir newD(QString::fromStdString(fixture.tempDir.string()) + "/.cadventory/jobs/new");
        const QDir curD(QString::fromStdString(fixture.tempDir.string()) + "/.cadventory/jobs/cur");
        const QDir doneD(QString::fromStdString(fixture.tempDir.string()) + "/.cadventory/jobs/done");
        DirectoryScanner counter;   // scanner w/o spool is just a counter
        REQUIRE(counter.scan(newD.canonicalPath().toStdString()).scanned == 0);
        REQUIRE(counter.scan(curD.canonicalPath().toStdString()).scanned == 0);
        REQUIRE(counter.scan(doneD.canonicalPath().toStdString()).scanned == FILES);

        // force sync
        spool.syncWithRepo();
        spool2.syncWithRepo();

        // combined repos should account for all files
        REQUIRE((repo.flushed + repo2.flushed) == FILES);
    }
}

TEST_CASE("syncWithRepo pulls external done jobs","[spool]") {
    struct RepoStub : IModelRepository {
        std::vector<QString> toPush;
        std::vector<QString> toPull;

        void markDoneBatch(const std::vector<QString>& paths) override {
            pushed = paths;
        }
        std::vector<QString> fetchPendingBatch() override {
            return toPull;
        }
        std::vector<QString> pushed;
    } repo;

    ModelTestFixture fixture(TEST_NAME);

    // setup dummy file(s)
    const QString root = makeTestLibrary(fixture.tempDir, 1);
    QString dummyFileName = root + "part_000.g";
    
    JobSpoolService spool(QString::fromStdString(fixture.tempDir.string()), repo);
    
    // Queue it and mark it done in repo without local markDone()
    REQUIRE(spool.enqueueJob(dummyFileName));
    repo.toPull = { dummyFileName };      // simulate another host finished it

    // force sync
    spool.syncWithRepo();

    // there should be no job to take
    QString jobFile;
    REQUIRE(spool.takeJob(&jobFile).isEmpty());

    // trying to enqueue again should fail
    REQUIRE_FALSE(spool.enqueueJob(dummyFileName));

    // should not have pushed anything
    REQUIRE(repo.pushed.empty());
}

TEST_CASE("autoMaintenence prunes old done jobs","[spool]") {
    ModelTestFixture fixture(TEST_NAME);
    int argc = 0;
    QCoreApplication app(argc, nullptr);

    QString root = QString::fromStdString(fixture.tempDir.string());

    DummyRepo repo;
    JobSpoolService spool(root, repo);

    // build the bucketPath for DONE (make sure it's in the first batch of buckets)
    QString bucket = QString("%1/.cadventory/jobs/done/00/00").arg(root);
    QDir().mkpath(bucket);
    // create two tombstones
    for (int i = 0; i < 2; ++i) {
        QString f = bucket + QString("/x%1.job").arg(i);
        QFile q(f);
        q.open(QIODevice::WriteOnly); 
        q.close();

        // only backdate one file
        if (i)
            continue;
        // backdate
        QDateTime oldDate = QDateTime::fromString("2000-01-01T12:00:00", Qt::ISODate);
#ifdef Q_OS_WIN
        HANDLE hFile = CreateFileW(reinterpret_cast<LPCWSTR>(f.utf16()),
                                   GENERIC_WRITE, FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            qWarning() << "Failed to open file for timestamp modification.";
            return;
        }

        FILETIME ft;
        SYSTEMTIME st;
        QDateTime utcDate = oldDate.toUTC();
        st.wYear = utcDate.date().year();
        st.wMonth = utcDate.date().month();
        st.wDay = utcDate.date().day();
        st.wHour = utcDate.time().hour();
        st.wMinute = utcDate.time().minute();
        st.wSecond = utcDate.time().second();
        st.wMilliseconds = 0;

        if (SystemTimeToFileTime(&st, &ft)) {
            SetFileTime(hFile, nullptr, nullptr, &ft);
        } else {
            qWarning() << "Failed to set file time.";
        }
        CloseHandle(hFile);
#else
        struct utimbuf newTimes;
        newTimes.actime = oldDate.toTime_t();  // access time
        newTimes.modtime = oldDate.toTime_t(); // modification time
        if (utime(f.toUtf8().constData(), &newTimes) != 0) {
            qWarning() << "Failed to set file time.";
        }
#endif
    }

    // make sure we have 2 files
    QDir d(bucket);
    REQUIRE(d.entryList(QDir::Files).size() == 2);

    // start maintenence timer, tick every ms
    spool.startAutoMaintenence(1);

    // assume delete janitor is called with (tick % 60) = 60ms
    QTimer::singleShot(100, &app, &QCoreApplication::quit);
    app.exec();

    // TODO: make a dummy file and see if it was auto-synced

    // make sure janitor deleted 'old' done job
    REQUIRE(d.entryList(QDir::Files).size() == 1);
}

// TODO: test that two filenames from different libraries hash to separate jobs