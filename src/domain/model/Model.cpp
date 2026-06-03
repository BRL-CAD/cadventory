#include "Model.h"

#include <QBuffer>
#include <QDebug>
#include <QVariant>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <sys/stat.h>

#include "Logger.h"

#if CADVENTORY_WITH_GUI
#include <QImageReader>
#include <QImageWriter>
#include <QPixmap>
#endif

namespace fs = std::filesystem;

namespace {

bool tableHasColumn(sqlite3* db, const char* tableName, const char* columnName) {
  const std::string sql = "PRAGMA table_info(" + std::string(tableName) + ");";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  bool found = false;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const unsigned char* txt = sqlite3_column_text(stmt, 1);
    if (txt && std::string(reinterpret_cast<const char*>(txt)) == columnName) {
      found = true;
      break;
    }
  }

  sqlite3_finalize(stmt);
  return found;
}

std::string getTextColumn(sqlite3_stmt* stmt, int col) {
  const unsigned char* text = sqlite3_column_text(stmt, col);
  return text ? reinterpret_cast<const char*>(text) : std::string{};
}

void populateModelFromRow(sqlite3_stmt* stmt, ModelData& model) {
  model.id = sqlite3_column_int(stmt, 0);
  model.short_name = getTextColumn(stmt, 1);
  model.primary_file = getTextColumn(stmt, 2);
  model.override_info = getTextColumn(stmt, 3);
  model.title = getTextColumn(stmt, 4);

  const void* blob = sqlite3_column_blob(stmt, 5);
  const int blob_size = sqlite3_column_bytes(stmt, 5);
  if (blob && blob_size > 0) {
    const char* blob_cast = static_cast<const char*>(blob);
    model.thumbnail.assign(blob_cast, blob_cast + blob_size);
  } else {
    model.thumbnail.clear();
  }

  model.author = getTextColumn(stmt, 6);
  model.long_name = getTextColumn(stmt, 7);
  model.modelers = getTextColumn(stmt, 8);
  model.model_type = getTextColumn(stmt, 9);
  model.aliases = getTextColumn(stmt, 10);
  model.suitability = getTextColumn(stmt, 11);
  model.classification = getTextColumn(stmt, 12);
  model.owner_org = getTextColumn(stmt, 13);
  model.source_org = getTextColumn(stmt, 14);
  model.file_path = getTextColumn(stmt, 15);
  model.library_name = getTextColumn(stmt, 16);
  model.created_at_fs = getTextColumn(stmt, 17);
  model.modified_at_fs = getTextColumn(stmt, 18);
  model.is_selected = sqlite3_column_int(stmt, 19) != 0;
  model.is_processed = sqlite3_column_int(stmt, 20) != 0;
  model.is_included = sqlite3_column_int(stmt, 21) != 0;
  model.syncMetadataAliases();
}

std::string canonicalLongName(const ModelData& modelData) {
  return modelData.long_name.empty() ? modelData.title : modelData.long_name;
}

std::string canonicalModelers(const ModelData& modelData) {
  return modelData.modelers.empty() ? modelData.author : modelData.modelers;
}

std::string compatibilityTitle(const ModelData& modelData) {
  return modelData.title.empty() ? canonicalLongName(modelData) : modelData.title;
}

std::string compatibilityAuthor(const ModelData& modelData) {
  return modelData.author.empty() ? canonicalModelers(modelData) : modelData.author;
}

std::string formatTimestampUtc(std::time_t value) {
  if (value <= 0) {
    return {};
  }

  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &value);
#else
  gmtime_r(&value, &tm);
#endif

  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return oss.str();
}

std::optional<std::time_t> modifiedTimeForPath(const fs::path& filePath) {
  std::error_code ec;
  const auto ft = fs::last_write_time(filePath, ec);
  if (ec) {
    return std::nullopt;
  }

  const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
      ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
  return std::chrono::system_clock::to_time_t(sctp);
}

std::optional<std::time_t> createdTimeForPath(const fs::path& filePath) {
#if defined(__APPLE__)
  struct stat st{};
  if (::stat(filePath.c_str(), &st) == 0 && st.st_birthtimespec.tv_sec > 0) {
    return static_cast<std::time_t>(st.st_birthtimespec.tv_sec);
  }
#endif
  return std::nullopt;
}

struct DerivedFsTimestamps {
  std::string created_at_fs;
  std::string modified_at_fs;
};

DerivedFsTimestamps deriveFsTimestamps(const ModelData& modelData,
                                       const fs::path& filePath) {
  DerivedFsTimestamps out{modelData.created_at_fs, modelData.modified_at_fs};

  if (const auto modified = modifiedTimeForPath(filePath)) {
    out.modified_at_fs = formatTimestampUtc(*modified);
  }

  if (const auto created = createdTimeForPath(filePath)) {
    out.created_at_fs = formatTimestampUtc(*created);
  } else if (out.created_at_fs.empty()) {
    out.created_at_fs = out.modified_at_fs;
  }

  return out;
}

}  // namespace

Model::Model(const std::string& libraryPath, QObject* parent)
    : QAbstractListModel(parent), db(nullptr) {
  // Create a hidden directory inside the library path
  hiddenPaths.setLibraryRoot(libraryPath);
  // Set the database path inside the hidden directory
  std::string dbPath = hiddenPaths.modelDb();
  std::string lockPath = dbPath + ".lock";
  dbFileLock.setPath(lockPath);

  if (sqlite3_open_v2(dbPath.c_str(), &db,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                      nullptr) != SQLITE_OK) {
    LOG_ERR << "Can't open database at " << dbPath << ": "
              << sqlite3_errmsg(db) << LOG_ENDL;
  } else {
    LOG_DEBUG << "Opened database at " << dbPath << " successfully" << LOG_ENDL;

    sqlite3_busy_timeout(db, 5000); // 5s
    sqlite3_exec(db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA journal_mode=DELETE;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA synchronous=FULL;",   nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA mmap_size=0;",        nullptr, nullptr, nullptr);

    createTables();
  }
}

Model::~Model() {
  if (db) {
    sqlite3_close_v2(db);
  }
}

bool Model::createTables() {
  LOG_DEBUG << "Creating tables..." << LOG_ENDL;
  std::string sqlModels = R"(
        CREATE TABLE IF NOT EXISTS models (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            short_name TEXT NOT NULL UNIQUE,
            primary_file TEXT,
            override_info TEXT,
            title TEXT,
            thumbnail BLOB,
            author TEXT,
            long_name TEXT,
            modelers TEXT,
            model_type TEXT,
            aliases TEXT,
            suitability TEXT,
            classification TEXT,
            owner_org TEXT,
            source_org TEXT,
            file_path TEXT UNIQUE,
            library_name TEXT,
            created_at_fs TEXT,
            modified_at_fs TEXT,
            is_selected INTEGER DEFAULT 0,
            is_processed INTEGER DEFAULT 0,
            is_included INTEGER DEFAULT 0
        );
    )";

  std::string sqlObjects = R"(
        CREATE TABLE IF NOT EXISTS objects (
            object_id INTEGER PRIMARY KEY AUTOINCREMENT,
            model_id INTEGER NOT NULL,
            name TEXT NOT NULL,
            parent_object_id INTEGER,
            is_selected INTEGER DEFAULT 0,
            FOREIGN KEY(model_id) REFERENCES models(id),
            FOREIGN KEY(parent_object_id) REFERENCES objects(object_id),
            UNIQUE (model_id, name)
        );
    )";

  std::string sqlTags = R"(
      CREATE TABLE IF NOT EXISTS tags (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          name TEXT NOT NULL UNIQUE
      );
  )";
  std::string sqlModelTags = R"(
      CREATE TABLE IF NOT EXISTS model_tags (
          model_id INTEGER NOT NULL,
          tag_id INTEGER NOT NULL,
          PRIMARY KEY (model_id, tag_id),
          FOREIGN KEY (model_id) REFERENCES models(id) ON DELETE CASCADE,
          FOREIGN KEY (tag_id) REFERENCES tags(id) ON DELETE CASCADE
      );
  )";

  const bool ok = executeSQL(sqlModels) && executeSQL(sqlObjects) &&
                  executeSQL(sqlTags) && executeSQL(sqlModelTags);
  if (!ok)
    return false;

  if (!tableHasColumn(db, "models", "long_name") &&
      !executeSQL("ALTER TABLE models ADD COLUMN long_name TEXT;")) {
    return false;
  }
  if (!tableHasColumn(db, "models", "modelers") &&
      !executeSQL("ALTER TABLE models ADD COLUMN modelers TEXT;")) {
    return false;
  }
  if (!tableHasColumn(db, "models", "model_type") &&
      !executeSQL("ALTER TABLE models ADD COLUMN model_type TEXT;")) {
    return false;
  }
  if (!tableHasColumn(db, "models", "aliases") &&
      !executeSQL("ALTER TABLE models ADD COLUMN aliases TEXT;")) {
    return false;
  }
  if (!tableHasColumn(db, "models", "suitability") &&
      !executeSQL("ALTER TABLE models ADD COLUMN suitability TEXT;")) {
    return false;
  }
  if (!tableHasColumn(db, "models", "classification") &&
      !executeSQL("ALTER TABLE models ADD COLUMN classification TEXT;")) {
    return false;
  }
  if (!tableHasColumn(db, "models", "owner_org") &&
      !executeSQL("ALTER TABLE models ADD COLUMN owner_org TEXT;")) {
    return false;
  }
  if (!tableHasColumn(db, "models", "source_org") &&
      !executeSQL("ALTER TABLE models ADD COLUMN source_org TEXT;")) {
    return false;
  }
  if (!tableHasColumn(db, "models", "created_at_fs") &&
      !executeSQL("ALTER TABLE models ADD COLUMN created_at_fs TEXT;")) {
    return false;
  }
  if (!tableHasColumn(db, "models", "modified_at_fs") &&
      !executeSQL("ALTER TABLE models ADD COLUMN modified_at_fs TEXT;")) {
    return false;
  }

  return true;
}

int Model::rowCount(const QModelIndex& parent) const {
  Q_UNUSED(parent);
  return static_cast<int>(models.size());
}

QVariant Model::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 ||
      index.row() >= static_cast<int>(models.size()))
    return QVariant();

  const ModelData& modelData = models.at(static_cast<size_t>(index.row()));

  switch (role) {
    case Qt::DisplayRole:
    case ShortNameRole:
      return QString::fromStdString(modelData.short_name);
    case IdRole:
      return modelData.id;
    case PrimaryFileRole:
      return QString::fromStdString(modelData.primary_file);
    case OverrideInfoRole:
      return QString::fromStdString(modelData.override_info);
    case TitleRole:
      return QString::fromStdString(modelData.effectiveLongName());
	case TagsRole: {
        std::vector<std::string> tagsFromDb = getTagsForModel(modelData.id);
        QStringList tagList;
        for (const std::string& tag : tagsFromDb) {
            tagList.append(QString::fromStdString(tag));
        }
        return tagList;
	}
    case ThumbnailRole:
#if CADVENTORY_WITH_GUI
      if (!modelData.thumbnail.empty()) {
        QPixmap thumbnail;
        thumbnail.loadFromData(
            reinterpret_cast<const uchar*>(modelData.thumbnail.data()),
            static_cast<uint>(modelData.thumbnail.size()), "PNG");
        return thumbnail;
      }
#endif
      return QVariant();
    case AuthorRole:
      return QString::fromStdString(modelData.effectiveModelers());
    case FilePathRole:
      return QString::fromStdString(modelData.file_path);
    case LibraryNameRole:
      return QString::fromStdString(modelData.library_name);
    case IsSelectedRole:
      return modelData.is_selected;
    case IsIncludedRole:
      return modelData.is_included;
    case IsProcessedRole:
      return modelData.is_processed;
    default:
      return QVariant();
  }
}

QHash<int, QByteArray> Model::roleNames() const {
  QHash<int, QByteArray> roles;
  roles[IdRole] = "id";
  roles[ShortNameRole] = "short_name";
  roles[PrimaryFileRole] = "primary_file";
  roles[OverrideInfoRole] = "override_info";
  roles[TitleRole] = "title";
  roles[ThumbnailRole] = "thumbnail";
  roles[AuthorRole] = "author";
  roles[FilePathRole] = "file_path";
  roles[LibraryNameRole] = "library_name";
  roles[IsSelectedRole] = "is_selected";
  roles[IsIncludedRole] = "is_included";
  roles[IsProcessedRole] = "is_processed";
  return roles;
}

bool Model::insertModel(const ModelData& modelData) {
  std::string sql = R"(
        INSERT INTO models
        (short_name, primary_file, override_info, title, thumbnail, author,
         long_name, modelers, model_type, aliases, suitability, classification,
         owner_org, source_org, file_path, library_name, created_at_fs,
         modified_at_fs, is_selected, is_processed, is_included)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";

  sqlite3_stmt* stmt;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);
  SimpleFileLock::Guard SFLock(dbFileLock);
  if (!SFLock) return false;

  // Ensure file_path is unique
  std::string generic_filepath = std::filesystem::path(modelData.file_path).generic_string();
  if (filePathExists(generic_filepath)) {
    LOG_INFO << "Model with file_path " << generic_filepath
             << " already exists." << LOG_ENDL;
    return false;
  }

  // Ensure short_name is unique by appending a suffix if necessary
  std::string short_name = modelData.short_name;
  int suffix = 1;
  int maxSuffix = 1000;  // Prevent infinite loop
  while (shortNameExists(short_name)) {
    if (suffix > maxSuffix) {
      LOG_ERR << "Error: Could not generate a unique short_name for "
                << modelData.short_name << LOG_ENDL;
      return false;
    }
    short_name = modelData.short_name + "_" + std::to_string(suffix++);
    LOG_DEBUG << "Generated new short_name:"
             << QString::fromStdString(short_name) << LOG_ENDL;
  }

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    const std::string long_name = canonicalLongName(modelData);
    const std::string modelers = canonicalModelers(modelData);
    const std::string title = compatibilityTitle(modelData);
    const std::string author = compatibilityAuthor(modelData);
    const auto fsTimes = deriveFsTimestamps(modelData, fs::path(modelData.file_path));

    // Bind parameters
    sqlite3_bind_text(stmt, 1, short_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, modelData.primary_file.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, modelData.override_info.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, title.c_str(), -1, SQLITE_TRANSIENT);

    if (!modelData.thumbnail.empty()) {
      sqlite3_bind_blob(stmt, 5, modelData.thumbnail.data(),
                        static_cast<int>(modelData.thumbnail.size()),
                        SQLITE_TRANSIENT);
    } else {
      sqlite3_bind_null(stmt, 5);
    }

    sqlite3_bind_text(stmt, 6, author.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, long_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, modelers.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, modelData.model_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, modelData.aliases.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, modelData.suitability.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, modelData.classification.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 13, modelData.owner_org.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 14, modelData.source_org.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 15, generic_filepath.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 16, modelData.library_name.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 17, fsTimes.created_at_fs.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 18, fsTimes.modified_at_fs.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 19, modelData.is_selected ? 1 : 0);
    sqlite3_bind_int(stmt, 20, modelData.is_processed ? 1 : 0);
    sqlite3_bind_int(stmt, 21, modelData.is_included ? 1 : 0);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
      LOG_ERR << "Insert model failed: " << sqlite3_errmsg(db) << LOG_ENDL;
      sqlite3_finalize(stmt);
      return false;
    }
    int id = static_cast<int>(sqlite3_last_insert_rowid(db));
    sqlite3_finalize(stmt);

    ModelData modelDataWithId = modelData;
    modelDataWithId.id = id;
    modelDataWithId.short_name = short_name;
    modelDataWithId.file_path = generic_filepath;
    modelDataWithId.long_name = long_name;
    modelDataWithId.modelers = modelers;
    modelDataWithId.model_type = modelData.model_type;
    modelDataWithId.aliases = modelData.aliases;
    modelDataWithId.suitability = modelData.suitability;
    modelDataWithId.classification = modelData.classification;
    modelDataWithId.owner_org = modelData.owner_org;
    modelDataWithId.source_org = modelData.source_org;
    modelDataWithId.created_at_fs = fsTimes.created_at_fs;
    modelDataWithId.modified_at_fs = fsTimes.modified_at_fs;
    modelDataWithId.title = title;
    modelDataWithId.author = author;
    modelDataWithId.syncMetadataAliases();

    beginInsertRows(QModelIndex(), models.size(), models.size());
    models.push_back(modelDataWithId);
    endInsertRows();

    LOG_DEBUG << "Model inserted successfully with id:" << id
              << ", short_name:" << QString::fromStdString(short_name)
              << LOG_ENDL;

    return true;
  } else {
    LOG_ERR << "SQL error in insertModel: " << sqlite3_errmsg(db) << LOG_ENDL;
    return false;
  }
}

bool Model::shortNameExists(const std::string& short_name) {
  std::string sql = "SELECT COUNT(*) FROM models WHERE short_name = ?;";
  sqlite3_stmt* stmt;
  int count = 0;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, short_name.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
      count = sqlite3_column_int(stmt, 0);
      LOG_DEBUG << "shortNameExists - count for"
                << QString::fromStdString(short_name) << ":" << count
                << LOG_ENDL;
    }
    sqlite3_finalize(stmt);
  } else {
    LOG_ERR << "SQL error in shortNameExists: " << sqlite3_errmsg(db) << LOG_ENDL;
  }

  return count > 0;
}

bool Model::filePathExists(const std::string& file_path) {
  std::string sql = "SELECT COUNT(*) FROM models WHERE file_path = ?;";
  sqlite3_stmt* stmt;
  int count = 0;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);

  LOG_DEBUG << "Checking if file_path exists:"
            << QString::fromStdString(file_path)
            << LOG_ENDL;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, file_path.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
      count = sqlite3_column_int(stmt, 0);
      LOG_DEBUG << "filePathExists - count for"
                << QString::fromStdString(file_path) << ":" << count
                << LOG_ENDL;
    }
    sqlite3_finalize(stmt);
  } else {
    LOG_ERR << "SQL error in filePathExists: " << sqlite3_errmsg(db) << LOG_ENDL;
  }

  return count > 0;
}

bool Model::updateModel(int id, const ModelData& modelData) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex);
    SimpleFileLock::Guard SFLock(dbFileLock);
    if (!SFLock) return false;

    // make sure we have an existing model to update
    // NOTE: it's the callers responsibility to manage creation vs updates
    auto result = getModelById(id);
    if (!result.has_value() || result.value().id != id)
        return false;
    ModelData existingModel = *result;

    // check for file_path conflicts
    if (filePathExists(modelData.file_path) && existingModel.file_path != modelData.file_path) {
        LOG_ERR << "Another model with file_path " << modelData.file_path
                << " already exists." << LOG_ENDL;
        return false;
    }

    // ensure short_name is unique if changed
    std::string short_name = modelData.short_name;
    int suffix = 1;
    while (shortNameExists(short_name) && existingModel.short_name != short_name) {
        short_name = modelData.short_name + "_" + std::to_string(suffix++);
    }

    std::string sql = R"(
        UPDATE models SET
            short_name = ?,
            primary_file = ?,
            override_info = ?,
            title = ?,
            thumbnail = ?,
            author = ?,
            long_name = ?,
            modelers = ?,
            model_type = ?,
            aliases = ?,
            suitability = ?,
            classification = ?,
            owner_org = ?,
            source_org = ?,
            file_path = ?,
            library_name = ?,
            created_at_fs = ?,
            modified_at_fs = ?,
            is_selected = ?,
            is_processed = ?,
            is_included = ?
        WHERE id = ?;
    )";

    // prepare stmt
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERR << "SQL error in updateModel: " << sqlite3_errmsg(db) << LOG_ENDL;
        return false;
    }

    const std::string long_name = canonicalLongName(modelData);
    const std::string modelers = canonicalModelers(modelData);
    const std::string title = compatibilityTitle(modelData);
    const std::string author = compatibilityAuthor(modelData);
    const auto fsTimes = deriveFsTimestamps(modelData, fs::path(modelData.file_path));

    // bind values
    sqlite3_bind_text(stmt, 1, short_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, modelData.primary_file.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, modelData.override_info.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, title.c_str(), -1, SQLITE_STATIC);

    if (!modelData.thumbnail.empty()) {
      sqlite3_bind_blob(stmt, 5, modelData.thumbnail.data(),
                        static_cast<int>(modelData.thumbnail.size()),
                        SQLITE_STATIC);
    } else {
      sqlite3_bind_null(stmt, 5);
    }

    sqlite3_bind_text(stmt, 6, author.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, long_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 8, modelers.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 9, modelData.model_type.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 10, modelData.aliases.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 11, modelData.suitability.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 12, modelData.classification.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 13, modelData.owner_org.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 14, modelData.source_org.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 15, modelData.file_path.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 16, modelData.library_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 17, fsTimes.created_at_fs.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 18, fsTimes.modified_at_fs.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 19, modelData.is_selected ? 1 : 0);
    sqlite3_bind_int(stmt, 20, modelData.is_processed ? 1 : 0);
    sqlite3_bind_int(stmt, 21, modelData.is_included ? 1 : 0);
    sqlite3_bind_int(stmt, 22, id);

    // do the sql update
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        LOG_ERR << "Update model failed: " << sqlite3_errmsg(db) << LOG_ENDL;
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_finalize(stmt);

    // ensure tags are updated too
    // first, delete old tags
    std::string sqlDeleteTags = "DELETE FROM model_tags WHERE model_id = ?;";
    sqlite3_stmt* deleteStmt = nullptr;
    if (sqlite3_prepare_v2(db, sqlDeleteTags.c_str(), -1, &deleteStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(deleteStmt, 1, id);
        if (sqlite3_step(deleteStmt) != SQLITE_DONE) {
            LOG_ERR << "Failed to delete existing tags: " << sqlite3_errmsg(db) << LOG_ENDL;
        }

        sqlite3_finalize(deleteStmt);
    } else {
        LOG_ERR << "SQL error in delete existing tags: " << sqlite3_errmsg(db) << LOG_ENDL;
    }
    // then, add the new tags
    for (const auto& tag : modelData.tags) {
        addTagToModel(id, tag);
    }

    // update the models vector in memory
    for (int row = 0; row < static_cast<int>(models.size()); ++row) {
        if (models[row].id == id) {
            models[row] = modelData;
            models[row].short_name = short_name;    // we might've changed short_name for collision avoidance
            models[row].long_name = long_name;
            models[row].modelers = modelers;
            models[row].model_type = modelData.model_type;
            models[row].aliases = modelData.aliases;
            models[row].suitability = modelData.suitability;
            models[row].classification = modelData.classification;
            models[row].owner_org = modelData.owner_org;
            models[row].source_org = modelData.source_org;
            models[row].created_at_fs = fsTimes.created_at_fs;
            models[row].modified_at_fs = fsTimes.modified_at_fs;
            models[row].title = title;
            models[row].author = author;
            models[row].syncMetadataAliases();

            QModelIndex modelIndex = index(row);
            emit dataChanged(modelIndex, modelIndex);
            break;
        }
    }

    return true;
}

bool Model::markAllNotIncluded() {
  std::lock_guard<std::recursive_mutex> lock(db_mutex);
  SimpleFileLock::Guard SFLock(dbFileLock);
  if (!SFLock) return false;
  const char* SQL = "UPDATE models SET is_included = 0 WHERE is_included <> 0;";
  char* err = nullptr;
  if (sqlite3_exec(db, SQL, nullptr, nullptr, &err) != SQLITE_OK) {
    LOG_ERR << "[Model::markAllNotIncluded] failed: " << err << LOG_ENDL;
    sqlite3_free(err);
    return false;
  }

  // refresh in-memory view
  loadModelsFromDatabase();
  return true;
}

bool Model::setModelIncluded(int id, bool included) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex);
  SimpleFileLock::Guard SFLock(dbFileLock);
  if (!SFLock) return false;

  static const char* SQL =
      "UPDATE models SET is_included = ?1 "
      "WHERE id = ?2 AND is_included <> ?1;";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, SQL, -1, &stmt, nullptr) != SQLITE_OK) {
    LOG_ERR << "setModelIncluded: prepare failed: " << sqlite3_errmsg(db) << LOG_ENDL;
    return false;
  }

  int rc = SQLITE_OK;
  rc |= sqlite3_bind_int(stmt, 1, included ? 1 : 0);
  rc |= sqlite3_bind_int(stmt, 2, id);
  if (rc != SQLITE_OK) {
    LOG_ERR << "setModelIncluded: bind failed" << LOG_ENDL;
    sqlite3_finalize(stmt);
    return false;
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    LOG_ERR << "setModelIncluded: step failed: " << sqlite3_errmsg(db) << LOG_ENDL;
    sqlite3_finalize(stmt);
    return false;
  }
  const int changed = sqlite3_changes(db);
  sqlite3_finalize(stmt);

  // update in-memory
  if (changed > 0) {
    for (int row = 0; row < static_cast<int>(models.size()); ++row) {
      if (models[row].id == id) {
        models[row].is_included = included;
        const QModelIndex modelIndex = index(row);

        emit dataChanged(modelIndex, modelIndex, { IsIncludedRole });
        break;
      }
    }
  }
  return true;
}

bool Model::setModelProcessed(int id, bool is_processed) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex);
    SimpleFileLock::Guard SFLock(dbFileLock);
    if (!SFLock) return false;

    static const char* SQL =
        "UPDATE models SET is_processed = ?1 WHERE id = ?2;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, SQL, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    int rc = SQLITE_OK;
    rc |= sqlite3_bind_int(stmt, 1, is_processed ? 1 : 0);
    rc |= sqlite3_bind_int(stmt, 2, id);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return false;
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE)
        return false;

    return true;
}

bool Model::selectAllIncluded(bool select) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex);
    SimpleFileLock::Guard SFLock(dbFileLock);
    if (!SFLock) return false;

    // update all included to is_selected = 'select'
    const char* SQL = "UPDATE models SET is_selected = ?1 WHERE is_included = 1;";
    sqlite3_stmt* stmt = nullptr;

    beginTransaction();

    if (sqlite3_prepare_v2(db, SQL, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERR << "selectAllIncluded: prepare failed: " << sqlite3_errmsg(db) << LOG_ENDL;
        commitTransaction();
        return false;
    }

    sqlite3_bind_int(stmt, 1, select ? 1 : 0);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        LOG_ERR << "selectAllIncluded: step failed: " << sqlite3_errmsg(db) << LOG_ENDL;
        sqlite3_finalize(stmt);
        commitTransaction();
        return false;
    }
    sqlite3_finalize(stmt);

    // update in-memory
    for (auto &m : models) {
        if (m.is_included) {
            m.is_selected = select;
        }
    }

    // emit data changed
    if (!models.empty()) {
        emit dataChanged(index(0), index(static_cast<int>(models.size()) - 1), { IsSelectedRole });
    }

    commitTransaction();
    return true;
}

bool Model::updateThumbnailFromFile(int id, const std::string pngPath) {
    std::vector<char> buff;
    bool haveBlob = false;

    if (!pngPath.empty()) {
        // make sure we can open the file
        std::ifstream file(pngPath, std::ios::binary);
        if (!file) {
            LOG_DEBUG << "[Model::updateThumbnailFromFile] open failed (" << pngPath << ") - clearing" << LOG_ENDL;
        } else {
            // read file
            file.seekg(0, std::ios::end);
            std::streamsize sz = file.tellg();
            if (sz <= 0) {
                LOG_DEBUG << "[Model::updateThumbnailFromFile] empty file (" << pngPath << ") - clearing" << LOG_ENDL;
            } else {
                // stuff into buffer
                file.seekg(0, std::ios::beg);   // reset seek
                buff.resize(static_cast<size_t>(sz));
                if (!file.read(buff.data(), sz)) {
                    LOG_DEBUG << "[Model::updateThumbnailFromFile] read failed (" << pngPath << ") - clearing" << LOG_ENDL;
                } else {
                    haveBlob = true;
                }
            }
        }
    } /* else: we want to clear the current thumbnail */

    // DB update
    std::lock_guard<std::recursive_mutex> lock(db_mutex);
    SimpleFileLock::Guard SFLock(dbFileLock);
    if (!SFLock) return false;

    static const char* SQL = "UPDATE models SET thumbnail = ?1 WHERE id = ?2;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, SQL, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERR << "updateThumbnailFromFile: prepare failed: " << sqlite3_errmsg(db) << LOG_ENDL;
        return false;
    }

    int rc = SQLITE_OK;
    if (haveBlob)
        rc |= sqlite3_bind_blob(stmt, 1, buff.data(), static_cast<int>(buff.size()), SQLITE_TRANSIENT);
    else
        rc |= sqlite3_bind_null(stmt, 1);
    rc |= sqlite3_bind_int(stmt,  2, id);
    if (rc != SQLITE_OK) {
        LOG_ERR << "updateThumbnailFromFile: bind failed" << LOG_ENDL;
        sqlite3_finalize(stmt);
        return false;
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        LOG_ERR << "updateThumbnailFromFile: step failed: " << sqlite3_errmsg(db) << LOG_ENDL;
        return false;
    }

    // update in-memory
    for (int row = 0; row < static_cast<int>(models.size()); ++row) {
        if (models[row].id == id) {
            if (haveBlob)
                models[row].thumbnail.assign(buff.begin(), buff.end());
            else
                models[row].thumbnail.clear();

            QModelIndex modelIndex = index(row);
            emit dataChanged(modelIndex, modelIndex, { ThumbnailRole });
            break;
        }
    }

    return true;
}

bool Model::deleteModel(int id) {
  // First, delete associated objects
  if (!deleteObjectsForModel(id)) {
    return false;
  }

  std::string sql = "DELETE FROM models WHERE id = ?;";
  sqlite3_stmt* stmt;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);
  SimpleFileLock::Guard SFLock(dbFileLock);
  if (!SFLock) return false;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    sqlite3_bind_int(stmt, 1, id);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
      LOG_ERR << "Delete model failed: " << sqlite3_errmsg(db) << LOG_ENDL;
      sqlite3_finalize(stmt);
      return false;
    }
    sqlite3_finalize(stmt);

    // Remove from models vector
    for (int row = 0; row < static_cast<int>(models.size()); ++row) {
      if (models[row].id == id) {
        beginRemoveRows(QModelIndex(), row, row);
        models.erase(models.begin() + row);
        endRemoveRows();
        break;
      }
    }

    return true;
  } else {
    LOG_ERR << "SQL error in deleteModel: " << sqlite3_errmsg(db) << LOG_ENDL;
    return false;
  }
}

bool Model::modelExists(int id) {
  std::string sql = "SELECT COUNT(*) FROM models WHERE id = ?;";
  sqlite3_stmt* stmt;
  int count = 0;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    sqlite3_bind_int(stmt, 1, id);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
      count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
  } else {
    LOG_ERR << "SQL error in modelExists: " << sqlite3_errmsg(db) << LOG_ENDL;
  }

  return count > 0;
}

std::optional<ModelData> Model::getModelById(int id) {
    std::string sql = R"(
        SELECT id, short_name, primary_file, override_info, title, thumbnail,
               author, long_name, modelers, model_type, aliases, suitability,
               classification, owner_org, source_org, file_path, library_name,
               created_at_fs, modified_at_fs,
               is_selected, is_processed, is_included
        FROM models WHERE id = ?;
    )";

    sqlite3_stmt* stmt = NULL;
    std::lock_guard<std::recursive_mutex> lock(db_mutex);

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERR << "SQL prepare error in getModelById: " << sqlite3_errmsg(db) << LOG_ENDL;
        return std::nullopt;
    }

    sqlite3_bind_int(stmt, 1, id);
    ModelData model;

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        LOG_ERR << "Failed to select model: " << sqlite3_errmsg(db) << LOG_ENDL;
        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    populateModelFromRow(stmt, model);

    sqlite3_finalize(stmt);
    return model;
}

ModelData Model::getModelByFilePath(const std::string& filePath) {
  ModelData model;
  model.id = 0;
  std::string sql = R"(
        SELECT id, short_name, primary_file, override_info, title, thumbnail,
               author, long_name, modelers, model_type, aliases, suitability,
               classification, owner_org, source_org, file_path, library_name,
               created_at_fs, modified_at_fs,
               is_selected, is_processed, is_included
        FROM models WHERE file_path = ?;
    )";
  sqlite3_stmt* stmt;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);

  // standardize filePath separators
  std::string generic_filepath = std::filesystem::path(filePath).generic_string();

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    // Use SQLITE_TRANSIENT to ensure SQLite makes its own copy of the data
    sqlite3_bind_text(stmt, 1, generic_filepath.c_str(), -1, SQLITE_TRANSIENT);

    // Debugging statements
    // LOG_DEBUG << "Executing SQL:" << QString::fromStdString(sql) << LOG_ENDL;
    // LOG_DEBUG << "With filePath:" << QString::fromStdString(filePath) << LOG_ENDL;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
      populateModelFromRow(stmt, model);

      LOG_DEBUG << "Model found with id:" << model.id
                << "and filePath:" << QString::fromStdString(model.file_path)
                << LOG_ENDL;
    } else {
      LOG_DEBUG << "No model found with filePath:"
                << QString::fromStdString(generic_filepath)
                << LOG_ENDL;
    }

    sqlite3_finalize(stmt);
  } else {
    LOG_ERR << "Failed to select model by file path: " << sqlite3_errmsg(db) << LOG_ENDL;
  }

  return model;
}

void Model::loadModelsFromDatabase() {
  std::vector<ModelData> loadedModels;
  std::string sql = R"(
        SELECT id, short_name, primary_file, override_info, title, thumbnail,
               author, long_name, modelers, model_type, aliases, suitability,
               classification, owner_org, source_org, file_path, library_name,
               created_at_fs, modified_at_fs,
               is_selected, is_processed, is_included
        FROM models;
    )";
  sqlite3_stmt* stmt;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      ModelData model;
      populateModelFromRow(stmt, model);
      loadedModels.push_back(model);
    }
    sqlite3_finalize(stmt);

    // Update the models vector
    beginResetModel();
    models = std::move(loadedModels);
    endResetModel();

  } else {
    LOG_ERR << "Failed to select models: " << sqlite3_errmsg(db) << LOG_ENDL;
  }
}

int Model::hashModel(const std::string& modelDir) {
  LOG_DEBUG << "hashModel called with modelDir:"
            << QString::fromStdString(modelDir)
            << LOG_ENDL;

  std::ifstream file(modelDir, std::ios::binary);
  if (!file.is_open()) {
    LOG_ERR << "Could not open file for hashing: " << modelDir << LOG_ENDL;
    return 0;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string fileContents = buffer.str();

  std::hash<std::string> hasher;
  int hashValue = static_cast<int>(hasher(fileContents));

  LOG_DEBUG << "Hash value for" << QString::fromStdString(modelDir) << ":"
            << hashValue << LOG_ENDL;

  return hashValue;
}

void Model::printModel(const ModelData& modelData) {
  LOG_DEBUG << "Model ID: " << modelData.id << LOG_ENDL;
  LOG_DEBUG << "Short Name: " << modelData.short_name << LOG_ENDL;
  LOG_DEBUG << "Primary File: " << modelData.primary_file << LOG_ENDL;
  LOG_DEBUG << "Override Info: " << modelData.override_info << LOG_ENDL;
  LOG_DEBUG << "Title: " << modelData.title << LOG_ENDL;
  LOG_DEBUG << "Author: " << modelData.author << LOG_ENDL;
  LOG_DEBUG << "Long Name: " << modelData.long_name << LOG_ENDL;
  LOG_DEBUG << "Modelers: " << modelData.modelers << LOG_ENDL;
  LOG_DEBUG << "Model Type: " << modelData.model_type << LOG_ENDL;
  LOG_DEBUG << "Suitability: " << modelData.suitability << LOG_ENDL;
  LOG_DEBUG << "Classification: " << modelData.classification << LOG_ENDL;
  LOG_DEBUG << "Owner Org: " << modelData.owner_org << LOG_ENDL;
  LOG_DEBUG << "Source Org: " << modelData.source_org << LOG_ENDL;
  LOG_DEBUG << "Created At FS: " << modelData.created_at_fs << LOG_ENDL;
  LOG_DEBUG << "Modified At FS: " << modelData.modified_at_fs << LOG_ENDL;
  LOG_DEBUG << "File Path: " << modelData.file_path << LOG_ENDL;
  LOG_DEBUG << "Library Name: " << modelData.library_name << LOG_ENDL;
  LOG_DEBUG << "Is Selected: " << (modelData.is_selected ? "Yes" : "No")
            << LOG_ENDL;
  LOG_DEBUG << "Thumbnail Size: " << modelData.thumbnail.size() << " bytes"
            << LOG_ENDL;
}

bool Model::executeSQL(const std::string& sql) {
  char* errMsg = nullptr;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);
  SimpleFileLock::Guard SFLock(dbFileLock);
  if (!SFLock) return false;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
  if (rc != SQLITE_OK) {
    LOG_ERR << "SQL error in executeSQL: " << errMsg << LOG_ENDL;
    sqlite3_free(errMsg);
    return false;
  }
  return true;
}

void Model::refreshModelData() { loadModelsFromDatabase(); }

bool Model::setData(const QModelIndex& index, const QVariant& value, int role) {
  if (!index.isValid() || index.row() < 0 ||
      index.row() >= static_cast<int>(models.size()))
    return false;

  ModelData& modelData = models[index.row()];

  if (role == IsSelectedRole) {
    modelData.is_selected = value.toBool();

    std::string sql = "UPDATE models SET is_selected = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    std::lock_guard<std::recursive_mutex> lock(db_mutex);
    SimpleFileLock::Guard SFLock(dbFileLock);
    if (!SFLock) return false;

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
      sqlite3_bind_int(stmt, 1, modelData.is_selected ? 1 : 0);
      sqlite3_bind_int(stmt, 2, modelData.id);

      if (sqlite3_step(stmt) != SQLITE_DONE) {
        LOG_ERR << "Failed to update is_selected in database: "
                  << sqlite3_errmsg(db) << LOG_ENDL;
        sqlite3_finalize(stmt);
        return false;
      }
      sqlite3_finalize(stmt);
    } else {
      LOG_ERR << "SQL error in setData when updating is_selected: "
                << sqlite3_errmsg(db) << LOG_ENDL;
      return false;
    }

    emit dataChanged(index, index, {IsSelectedRole});
    return true;
  } else if (role == IsIncludedRole) {
    modelData.is_included = value.toBool();

    std::string sql = "UPDATE models SET is_included = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    std::lock_guard<std::recursive_mutex> lock(db_mutex);

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
      sqlite3_bind_int(stmt, 1, modelData.is_included ? 1 : 0);
      sqlite3_bind_int(stmt, 2, modelData.id);

      if (sqlite3_step(stmt) != SQLITE_DONE) {
        LOG_ERR << "Failed to update is_included in database: "
                  << sqlite3_errmsg(db) << LOG_ENDL;
        sqlite3_finalize(stmt);
        return false;
      }
      sqlite3_finalize(stmt);
    } else {
      LOG_ERR << "SQL error in setData when updating is_included: "
                << sqlite3_errmsg(db) << LOG_ENDL;
      return false;
    }

    emit dataChanged(index, index, {IsIncludedRole});
    return true;
  }

  return false;
}

Qt::ItemFlags Model::flags(const QModelIndex& index) const {
  if (!index.isValid()) return Qt::NoItemFlags;

  return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

// Object Operations
int Model::insertObject(const ObjectData& obj) {
  std::string upsert_sql = R"(
        INSERT INTO objects (model_id, name, parent_object_id, is_selected)
        VALUES (?, ?, ?, ?)
        ON CONFLICT(model_id, name) DO UPDATE SET
          parent_object_id = excluded.parent_object_id,
          is_selected = excluded.is_selected
        RETURNING object_id
    )";

  sqlite3_stmt* stmt;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);
  SimpleFileLock::Guard SFLock(dbFileLock);
  if (!SFLock) return -1;

  if (sqlite3_prepare_v2(db, upsert_sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    LOG_ERR << "SQL error in insertObject: " << sqlite3_errmsg(db) << LOG_ENDL;
    return -1;
  }

  sqlite3_bind_int(stmt, 1, obj.model_id);
  sqlite3_bind_text(stmt, 2, obj.name.c_str(), -1, SQLITE_TRANSIENT);

  if (obj.parent_object_id != -1) {
    sqlite3_bind_int(stmt, 3, obj.parent_object_id);
  } else {
    sqlite3_bind_null(stmt, 3);
  }

  sqlite3_bind_int(stmt, 4, obj.is_selected ? 1 : 0);

  int object_id = -1;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    object_id = sqlite3_column_int(stmt, 0);
  } else {
    LOG_ERR << "insertObject step failed: " << sqlite3_errmsg(db) << LOG_ENDL;
  }

  sqlite3_finalize(stmt);
  return object_id;
}

bool Model::deleteObjectsForModel(int model_id) {
  std::string sql = "DELETE FROM objects WHERE model_id = ?;";
  sqlite3_stmt* stmt;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);
  SimpleFileLock::Guard SFLock(dbFileLock);
  if (!SFLock) return false;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    sqlite3_bind_int(stmt, 1, model_id);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
      LOG_ERR << "Delete objects failed: " << sqlite3_errmsg(db) << LOG_ENDL;
      sqlite3_finalize(stmt);
      return false;
    }
    sqlite3_finalize(stmt);
    return true;
  } else {
    LOG_ERR << "SQL error in deleteObjectsForModel: " << sqlite3_errmsg(db) << LOG_ENDL;
    return false;
  }
}

std::vector<ObjectData> Model::getObjectsForModel(int model_id) {
  std::vector<ObjectData> objects;
  std::string sql = R"(
        SELECT object_id, model_id, name, parent_object_id, is_selected
        FROM objects
        WHERE model_id = ?;
    )";

  sqlite3_stmt* stmt;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    sqlite3_bind_int(stmt, 1, model_id);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      ObjectData obj;
      obj.object_id = sqlite3_column_int(stmt, 0);
      obj.model_id = sqlite3_column_int(stmt, 1);
      obj.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

      if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
        obj.parent_object_id = sqlite3_column_int(stmt, 3);
      } else {
        obj.parent_object_id = -1;
      }

      obj.is_selected = sqlite3_column_int(stmt, 4) != 0;
      objects.push_back(obj);
    }
    sqlite3_finalize(stmt);
  } else {
    LOG_ERR << "Failed to retrieve objects: " << sqlite3_errmsg(db) << LOG_ENDL;
  }

  return objects;
}

bool Model::setObjectData(int object_id, const QVariant& value, int role) {
  if (role == IsSelectedRole) {
    bool is_selected = value.toBool();
    return updateObjectSelection(object_id, is_selected);
  }
  return false;
}

bool Model::updateObjectSelection(int object_id, bool is_selected) {
  std::string sql = "UPDATE objects SET is_selected = ? WHERE object_id = ?;";

  sqlite3_stmt* stmt;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);
  SimpleFileLock::Guard SFLock(dbFileLock);
  if (!SFLock) return false;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    LOG_ERR << "SQL error in updateObjectSelection: " << sqlite3_errmsg(db) << LOG_ENDL;
    return false;
  }

  sqlite3_bind_int(stmt, 1, is_selected ? 1 : 0);
  sqlite3_bind_int(stmt, 2, object_id);

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    LOG_ERR << "Update object selection failed: " << sqlite3_errmsg(db) << LOG_ENDL;
    sqlite3_finalize(stmt);
    return false;
  }

  sqlite3_finalize(stmt);
  return true;
}

bool Model::updateObject(const ObjectData& obj) {
  std::string sql = R"(
        UPDATE objects SET
            name = ?,
            parent_object_id = ?,
            is_selected = ?
        WHERE object_id = ?;
    )";

  sqlite3_stmt* stmt;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);
  SimpleFileLock::Guard SFLock(dbFileLock);
  if (!SFLock) return false;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    LOG_ERR << "SQL error in updateObject: " << sqlite3_errmsg(db) << LOG_ENDL;
    return false;
  }

  sqlite3_bind_text(stmt, 1, obj.name.c_str(), -1, SQLITE_STATIC);
  if (obj.parent_object_id != -1) {
    sqlite3_bind_int(stmt, 2, obj.parent_object_id);
  } else {
    sqlite3_bind_null(stmt, 2);
  }
  sqlite3_bind_int(stmt, 3, obj.is_selected ? 1 : 0);
  sqlite3_bind_int(stmt, 4, obj.object_id);

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    LOG_ERR << "Update object failed: " << sqlite3_errmsg(db) << LOG_ENDL;
    sqlite3_finalize(stmt);
    return false;
  }

  sqlite3_finalize(stmt);
  return true;
}

ObjectData Model::getObjectById(int object_id) {
  ObjectData obj;
  std::string sql = R"(
        SELECT object_id, model_id, name, parent_object_id, is_selected
        FROM objects
        WHERE object_id = ?;
    )";

  sqlite3_stmt* stmt;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    sqlite3_bind_int(stmt, 1, object_id);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
      obj.object_id = sqlite3_column_int(stmt, 0);
      obj.model_id = sqlite3_column_int(stmt, 1);
      obj.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

      if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
        obj.parent_object_id = sqlite3_column_int(stmt, 3);
      } else {
        obj.parent_object_id = -1;
      }

      obj.is_selected = sqlite3_column_int(stmt, 4) != 0;
    } else {
      // Handle the case where the object is not found
      LOG_ERR << "Object with ID " << object_id << " not found." << LOG_ENDL;
    }

    sqlite3_finalize(stmt);
  } else {
    LOG_ERR << "SQL error in getObjectById: " << sqlite3_errmsg(db) << LOG_ENDL;
  }

  return obj;
}

std::vector<ModelData> Model::getSelectedModels() {
  std::vector<ModelData> selectedModels;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);

  for (const auto& modelData : models) {
    if (modelData.is_selected) {
      selectedModels.push_back(modelData);
    }
  }

  return selectedModels;
}

std::vector<ObjectData> Model::getSelectedObjectsForModel(int model_id) {
  std::vector<ObjectData> selectedObjects;
  std::string sql = R"(
        SELECT object_id, model_id, name, parent_object_id, is_selected
        FROM objects
        WHERE model_id = ? AND is_selected = 1;
    )";

  sqlite3_stmt* stmt;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    sqlite3_bind_int(stmt, 1, model_id);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      ObjectData obj;
      obj.object_id = sqlite3_column_int(stmt, 0);
      obj.model_id = sqlite3_column_int(stmt, 1);
      obj.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

      if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
        obj.parent_object_id = sqlite3_column_int(stmt, 3);
      } else {
        obj.parent_object_id = -1;
      }

      obj.is_selected = sqlite3_column_int(stmt, 4) != 0;
      selectedObjects.push_back(obj);
    }
    sqlite3_finalize(stmt);
  } else {
    LOG_ERR << "Failed to prepare statement in getSelectedObjectsForModel: "
              << sqlite3_errmsg(db) << LOG_ENDL;
  }

  return selectedObjects;
}

void Model::beginTransaction() { executeSQL("BEGIN IMMEDIATE;"); }

void Model::commitTransaction() { executeSQL("COMMIT;"); }

bool Model::updateObjectParentId(int object_id, int parent_object_id) {
  std::string sql =
      "UPDATE objects SET parent_object_id = ? WHERE object_id = ?;";
  sqlite3_stmt* stmt;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);
  SimpleFileLock::Guard SFLock(dbFileLock);
  if (!SFLock) return false;

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    LOG_ERR << "SQL error in updateObjectParentId: " << sqlite3_errmsg(db) << LOG_ENDL;
    return false;
  }

  sqlite3_bind_int(stmt, 1, parent_object_id);
  sqlite3_bind_int(stmt, 2, object_id);

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    LOG_ERR << "Update object parent ID failed: " << sqlite3_errmsg(db) << LOG_ENDL;
    sqlite3_finalize(stmt);
    return false;
  }

  sqlite3_finalize(stmt);
  return true;
}

bool Model::deleteTables() {
  std::string sqlDeleteModels = "DROP TABLE IF EXISTS models;";
  std::string sqlDeleteObjects = "DROP TABLE IF EXISTS objects;";

  // Execute SQL commands to delete tables
  return executeSQL(sqlDeleteModels) && executeSQL(sqlDeleteObjects);
}

void Model::resetDatabase() {
  if (deleteTables()) {  // Delete existing tables
    createTables();      // Recreate tables
    refreshModelData();  // Optional: Load initial data if necessary
  } else {
    LOG_ERR << "Failed to delete tables." << LOG_ENDL;
  }
}

// Tag Operations
bool Model::addTagToModel(int modelId, const std::string& tagName) {
  // Insert the tag if it doesn't already exist
  std::string sqlTagInsert = "INSERT OR IGNORE INTO tags (name) VALUES (?);";
  sqlite3_stmt* stmt = prepareStatement(sqlTagInsert);
  if (!stmt) return false;

  sqlite3_bind_text(stmt, 1, tagName.c_str(), -1, SQLITE_STATIC);
  if (!executePreparedStatement(stmt)) return false;

  // Get tag ID
  int tagId = getTagId(tagName);
  if (tagId == -1) return false;

  // Link the tag to the model
  std::string sqlLink =
      "INSERT INTO model_tags (model_id, tag_id) VALUES (?, ?);";
  stmt = prepareStatement(sqlLink);
  if (!stmt) return false;

  sqlite3_bind_int(stmt, 1, modelId);
  sqlite3_bind_int(stmt, 2, tagId);

  return executePreparedStatement(stmt);
}

int Model::getTagId(const std::string& tagName) {
  std::string sqlTagId = "SELECT id FROM tags WHERE name = ?;";
  sqlite3_stmt* stmt = prepareStatement(sqlTagId);
  if (!stmt) return -1;

  sqlite3_bind_text(stmt, 1, tagName.c_str(), -1, SQLITE_STATIC);
  int tagId = -1;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    tagId = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return tagId;
}

std::vector<std::string> Model::getAllTags() {
  std::vector<std::string> tags;
  std::string sql = "SELECT name FROM tags;";
  sqlite3_stmt* stmt = prepareStatement(sql);
  if (!stmt) return tags;

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* tagText =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    if (tagText) {
      tags.push_back(tagText);
    }
  }
  sqlite3_finalize(stmt);
  return tags;
}

std::vector<std::string> Model::getTagsForModel(int modelId) const {
  std::vector<std::string> tags;
  std::string sql =
      "SELECT name FROM tags t JOIN model_tags mt ON t.id = mt.tag_id WHERE "
      "mt.model_id = ?;";
  sqlite3_stmt* stmt = prepareStatement(sql);
  if (!stmt) return tags;

  sqlite3_bind_int(stmt, 1, modelId);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* tagText =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    if (tagText) {
      tags.push_back(tagText);
    }
  }
  sqlite3_finalize(stmt);
  return tags;
}

bool Model::removeTagFromModel(int modelId, const std::string& tagName) {
  int tagId = getTagId(tagName);
  if (tagId == -1) return false;

  // Delete the association in the model_tags table
  std::string sql = "DELETE FROM model_tags WHERE model_id = ? AND tag_id = ?;";
  sqlite3_stmt* stmt = prepareStatement(sql);
  if (!stmt) return false;

  sqlite3_bind_int(stmt, 1, modelId);
  sqlite3_bind_int(stmt, 2, tagId);

  LOG_DEBUG << "Removing tag " << tagName << " from model " << modelId
            << LOG_ENDL;

  return executePreparedStatement(stmt);
}

bool Model::removeAllTagsFromModel(int modelId) {
  std::string sql = "DELETE FROM model_tags WHERE model_id = ?;";
  sqlite3_stmt* stmt = prepareStatement(sql);
  if (!stmt) return false;

  sqlite3_bind_int(stmt, 1, modelId);

  return executePreparedStatement(stmt);
}

// Property Operations
std::map<std::string, std::string> Model::getPropertiesForModel(int modelId) {
  std::map<std::string, std::string> properties;
  const auto modelData = getModelById(modelId);
  if (!modelData.has_value())
    return properties;

  properties["short_name"] = modelData->short_name;
  properties["long_name"] = modelData->effectiveLongName();
  properties["modelers"] = modelData->effectiveModelers();
  properties["model_type"] = modelData->model_type;
  properties["aliases"] = modelData->aliases;
  properties["suitability"] = modelData->suitability;
  properties["classification"] = modelData->classification;
  properties["owner_org"] = modelData->owner_org;
  properties["source_org"] = modelData->source_org;
  properties["file_path"] = modelData->file_path;
  properties["library_name"] = modelData->library_name;
  properties["created_at_fs"] = modelData->created_at_fs;
  properties["modified_at_fs"] = modelData->modified_at_fs;
  return properties;
}

bool Model::setPropertyForModel(int modelId, const std::string& property,
                                const std::string& value) {
  // Validate that the property is an allowed column
  static const std::set<std::string> allowedProperties = {
      "short_name",  "primary_file", "override_info", "title",
      "author",      "file_path",    "library_name",  "long_name",
      "modelers",    "model_type",   "aliases",       "owner_org",
      "source_org",  "suitability",  "classification"};

  if (allowedProperties.find(property) == allowedProperties.end()) {
    LOG_ERR << "Invalid property name: " << property << LOG_ENDL;
    return false;
  }

  std::string sql;
  if (property == "long_name") {
    sql = "UPDATE models SET long_name = ?, title = ? WHERE id = ?;";
  } else if (property == "modelers") {
    sql = "UPDATE models SET modelers = ?, author = ? WHERE id = ?;";
  } else if (property == "title") {
    sql = "UPDATE models SET title = ?, long_name = ? WHERE id = ?;";
  } else if (property == "author") {
    sql = "UPDATE models SET author = ?, modelers = ? WHERE id = ?;";
  } else {
    sql = "UPDATE models SET " + property + " = ? WHERE id = ?;";
  }

  sqlite3_stmt* stmt = prepareStatement(sql);
  if (!stmt) return false;

  sqlite3_bind_text(stmt, 1, value.c_str(), -1, SQLITE_STATIC);
  if (property == "long_name" || property == "modelers" ||
      property == "title" || property == "author") {
    sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, modelId);
  } else {
    sqlite3_bind_int(stmt, 2, modelId);
  }

  if (!executePreparedStatement(stmt))
    return false;

  for (int row = 0; row < static_cast<int>(models.size()); ++row) {
    if (models[row].id != modelId)
      continue;

    if (property == "short_name") {
      models[row].short_name = value;
    } else if (property == "primary_file") {
      models[row].primary_file = value;
    } else if (property == "override_info") {
      models[row].override_info = value;
    } else if (property == "title") {
      models[row].title = value;
      models[row].long_name = value;
    } else if (property == "author") {
      models[row].author = value;
      models[row].modelers = value;
    } else if (property == "file_path") {
      models[row].file_path = value;
    } else if (property == "library_name") {
      models[row].library_name = value;
    } else if (property == "long_name") {
      models[row].long_name = value;
      models[row].title = value;
    } else if (property == "modelers") {
      models[row].modelers = value;
      models[row].author = value;
    } else if (property == "model_type") {
      models[row].model_type = value;
    } else if (property == "aliases") {
      models[row].aliases = value;
    } else if (property == "suitability") {
      models[row].suitability = value;
    } else if (property == "classification") {
      models[row].classification = value;
    } else if (property == "owner_org") {
      models[row].owner_org = value;
    } else if (property == "source_org") {
      models[row].source_org = value;
    }

    models[row].syncMetadataAliases();
    const QModelIndex modelIndex = index(row);
    emit dataChanged(modelIndex, modelIndex);
    break;
  }

  return true;
}

// Simplifying executions
sqlite3_stmt* Model::prepareStatement(const std::string& sql) const {
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    LOG_ERR << "Failed to prepare statement: " << sqlite3_errmsg(db) << LOG_ENDL;
    return nullptr;
  }
  return stmt;
}

bool Model::executePreparedStatement(sqlite3_stmt* stmt) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex);
  SimpleFileLock::Guard SFLock(dbFileLock);
  if (!SFLock) return false;
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    LOG_ERR << "Execution failed: " << sqlite3_errmsg(db) << LOG_ENDL;
    sqlite3_finalize(stmt);
    return false;
  }
  sqlite3_finalize(stmt);
  return true;
}

std::vector<ModelData> Model::getIncludedModels() {
  std::vector<ModelData> includedModels;
    std::string sql = R"(
        SELECT id, short_name, primary_file, override_info, title, thumbnail,
               author, long_name, modelers, model_type, aliases, suitability,
               classification, owner_org, source_org, file_path, library_name,
               created_at_fs, modified_at_fs,
               is_selected, is_processed, is_included
        FROM models
        WHERE is_included = 1;
    )";
  sqlite3_stmt* stmt;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      ModelData model;
      populateModelFromRow(stmt, model);
      includedModels.push_back(model);
    }
    sqlite3_finalize(stmt);
  } else {
    LOG_ERR << "Failed to select included models: " << sqlite3_errmsg(db) << LOG_ENDL;
  }

  return includedModels;
}

std::vector<ModelData> Model::getAll() {
    std::vector<ModelData> out;

    static const char* SQL = R"(
        SELECT id, short_name, primary_file, override_info, title,
               thumbnail, author, long_name, modelers, model_type, aliases,
               suitability, classification, owner_org, source_org, file_path,
               library_name, created_at_fs, modified_at_fs, is_selected,
               is_processed, is_included
        FROM models;
    )";

    sqlite3_stmt* stmt = nullptr;
    std::lock_guard<std::recursive_mutex> lock(db_mutex);

    if (sqlite3_prepare_v2(db, SQL, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERR << "[Model::getAll] SQL prepare error: "
                  << sqlite3_errmsg(db) << LOG_ENDL;
        return out;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ModelData m;
        populateModelFromRow(stmt, m);
        out.push_back(std::move(m));
    }

    sqlite3_finalize(stmt);
    return out;
}

bool Model::isFileIncluded(const std::string& filePath) {
  std::string sql = "SELECT is_included FROM models WHERE file_path = ?;";
  sqlite3_stmt* stmt;
  bool included = false;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, filePath.c_str(), -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      included = sqlite3_column_int(stmt, 0) != 0;
    }
    sqlite3_finalize(stmt);
  } else {
    LOG_ERR << "SQL error in isFileIncluded: " << sqlite3_errmsg(db) << LOG_ENDL;
  }

  return included;
}


std::vector<ModelData> Model::getIncludedNotProcessedModels() {
    std::vector<ModelData> notProcessedModels;

    const char* sql = R"(
        SELECT id, short_name, primary_file, override_info, title,
               thumbnail, author, long_name, modelers, model_type, aliases,
               suitability, classification, owner_org, source_org, file_path,
               library_name, created_at_fs, modified_at_fs, is_selected,
               is_processed, is_included
        FROM models
        WHERE is_included = 1 AND is_processed = 0;
    )";

    sqlite3_stmt* stmt;
    std::lock_guard<std::recursive_mutex> lock(db_mutex);

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ModelData modelData;
            populateModelFromRow(stmt, modelData);
            notProcessedModels.push_back(modelData);
        }

        sqlite3_finalize(stmt);
    } else {
        LOG_ERR << "[Model::getIncludedNotProcessedModels] SQL error: "
                  << sqlite3_errmsg(db) << LOG_ENDL;
    }

    return notProcessedModels;
}
