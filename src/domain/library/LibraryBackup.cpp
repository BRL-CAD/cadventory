#include "LibraryBackup.h"

#include <QDateTime>

#include <sqlite3.h>

namespace fs = std::filesystem;

namespace {

bool isWithin(const fs::path& child, const fs::path& parent) {
    auto childIt = child.begin();
    for (auto parentIt = parent.begin(); parentIt != parent.end(); ++parentIt, ++childIt) {
        if (childIt == child.end() || *childIt != *parentIt)
            return false;
    }
    return true;
}

bool backupSqliteDatabase(const fs::path& sourcePath, const fs::path& destinationPath) {
    sqlite3* source = nullptr;
    sqlite3* destination = nullptr;

    const int sourceOpen = sqlite3_open_v2(sourcePath.c_str(), &source, SQLITE_OPEN_READONLY, nullptr);
    const int destinationOpen = sqlite3_open_v2(destinationPath.c_str(), &destination,
                                                SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (sourceOpen != SQLITE_OK || destinationOpen != SQLITE_OK) {
        sqlite3_close(source);
        sqlite3_close(destination);
        return false;
    }

    sqlite3_busy_timeout(source, 5000);
    sqlite3_busy_timeout(destination, 5000);
    sqlite3_backup* backup = sqlite3_backup_init(destination, "main", source, "main");
    if (!backup) {
        sqlite3_close(source);
        sqlite3_close(destination);
        return false;
    }

    const int step = sqlite3_backup_step(backup, -1);
    const int finish = sqlite3_backup_finish(backup);
    sqlite3_close(source);
    sqlite3_close(destination);
    return step == SQLITE_DONE && finish == SQLITE_OK;
}

bool copyNonDatabaseFiles(const fs::path& sourceRoot,
                          const fs::path& destinationRoot,
                          const fs::path& modelDatabase,
                          const fs::path& jobsDatabase) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(sourceRoot, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path sourcePath = it->path();
        if (sourcePath == modelDatabase || sourcePath == jobsDatabase)
            continue;

        const fs::path relative = fs::relative(sourcePath, sourceRoot, ec);
        if (ec)
            return false;
        const fs::path destinationPath = destinationRoot / relative;

        if (it->is_directory(ec)) {
            fs::create_directories(destinationPath, ec);
        } else if (it->is_regular_file(ec)) {
            fs::create_directories(destinationPath.parent_path(), ec);
            if (!ec)
                fs::copy_file(sourcePath, destinationPath, fs::copy_options::overwrite_existing, ec);
        }
        if (ec)
            return false;
    }
    return !ec;
}

}

LibraryBackupResult LibraryBackup::create(const HiddenDir& paths,
                                          const fs::path& destinationRoot) {
    LibraryBackupResult result;
    std::error_code ec;
    const fs::path sourceRoot = fs::absolute(paths.dotFolder(), ec).lexically_normal();
    const fs::path outputRoot = fs::absolute(destinationRoot, ec).lexically_normal();
    if (ec || isWithin(outputRoot, sourceRoot)) {
        result.error = "Backup destination must be outside .cadventory.";
        return result;
    }

    const QString stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMddTHHmmsszzz");
    const fs::path finalDirectory = outputRoot / ("cadventory-backup-" + stamp.toStdString());
    const fs::path temporaryDirectory = finalDirectory.string() + ".tmp";
    const fs::path temporaryManagedRoot = temporaryDirectory / CADV_DOTFOLDER;

    fs::create_directories(temporaryManagedRoot, ec);
    if (ec) {
        result.error = "Could not create backup directory.";
        return result;
    }

    const fs::path modelDatabase = paths.modelDb();
    const fs::path jobsDatabase = paths.jobsDb();
    const bool copiedFiles = copyNonDatabaseFiles(sourceRoot, temporaryManagedRoot,
                                                   modelDatabase, jobsDatabase);
    const bool copiedModelDatabase = copiedFiles && backupSqliteDatabase(
        modelDatabase, temporaryManagedRoot / CADV_MODEL_DB);
    const bool copiedJobsDatabase = copiedModelDatabase && backupSqliteDatabase(
        jobsDatabase, temporaryManagedRoot / CADV_JOBS_DIR / CADV_JOBS_DB);
    if (!copiedJobsDatabase) {
        fs::remove_all(temporaryDirectory, ec);
        result.error = "Could not create a consistent database backup.";
        return result;
    }

    fs::rename(temporaryDirectory, finalDirectory, ec);
    if (ec) {
        fs::remove_all(temporaryDirectory, ec);
        result.error = "Could not finalize backup.";
        return result;
    }

    result.success = true;
    result.directory = finalDirectory;
    return result;
}
