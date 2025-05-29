#include "Model.h"

#include <QBuffer>
#include <QDebug>
#include <QImageReader>
#include <QImageWriter>
#include <QPixmap>
#include <QVariant>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

Model::Model(const std::string& libraryPath, QObject* parent)
    : QAbstractListModel(parent), db(nullptr) {
  // Create a hidden directory inside the library path
  fs::path hiddenDir = fs::path(libraryPath) / ".cadventory";
  hiddenDirPath = hiddenDir.string();
  if (!fs::exists(hiddenDir)) {
    fs::create_directory(hiddenDir);
  }

  // Set the database path inside the hidden directory
  dbPath = (hiddenDir / "metadata.db").string();

  if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
    std::cerr << "Can't open database at " << dbPath << ": "
              << sqlite3_errmsg(db) << std::endl;
  } else {
    std::cout << "Opened database at " << dbPath << " successfully"
              << std::endl;
    createTables();

    loadModelsFromDatabase();
  }
}

Model::~Model() {
  if (db) {
    sqlite3_close(db);
  }
}

bool Model::createTables() {
  std::cout << "Creating tables..." << std::endl;
  std::string sqlModels = R"(
        CREATE TABLE IF NOT EXISTS models (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            short_name TEXT NOT NULL UNIQUE,
            primary_file TEXT,
            override_info TEXT,
            title TEXT,
            thumbnail BLOB,
            author TEXT,
            file_path TEXT UNIQUE,
            library_name TEXT,
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
            FOREIGN KEY(parent_object_id) REFERENCES objects(object_id)
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

  return executeSQL(sqlModels) && executeSQL(sqlObjects) &&
         executeSQL(sqlTags) && executeSQL(sqlModelTags);
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
      return QString::fromStdString(modelData.title);
	case TagsRole: {
        std::vector<std::string> tagsFromDb = getTagsForModel(modelData.id);
        QStringList tagList;
        for (const std::string& tag : tagsFromDb) {
            tagList.append(QString::fromStdString(tag));
        }
        return tagList;
	}
    case ThumbnailRole:
      if (!modelData.thumbnail.empty()) {
        QPixmap thumbnail;
        thumbnail.loadFromData(
            reinterpret_cast<const uchar*>(modelData.thumbnail.data()),
            static_cast<uint>(modelData.thumbnail.size()), "PNG");
        return thumbnail;
      }
      return QVariant();
    case AuthorRole:
      return QString::fromStdString(modelData.author);
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
        (short_name, primary_file, override_info, title, thumbnail, author, file_path, library_name, is_selected, is_processed, is_included)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";

  sqlite3_stmt* stmt;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);

  // Ensure short_name is unique by appending a suffix if necessary
  std::string short_name = modelData.short_name;
  int suffix = 1;
  int maxSuffix = 1000;  // Prevent infinite loop
  while (shortNameExists(short_name)) {
    if (suffix > maxSuffix) {
      std::cerr << "Error: Could not generate a unique short_name for "
                << modelData.short_name << std::endl;
      return false;
    }
    short_name = modelData.short_name + "_" + std::to_string(suffix++);
    qDebug() << "Generated new short_name:"
             << QString::fromStdString(short_name);
  }

  // Ensure file_path is unique
  if (filePathExists(modelData.file_path)) {
    std::cerr << "Model with file_path " << modelData.file_path
              << " already exists." << std::endl;
    return false;
  }

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    // Bind parameters
    sqlite3_bind_text(stmt, 1, short_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, modelData.primary_file.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, modelData.override_info.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, modelData.title.c_str(), -1, SQLITE_TRANSIENT);

    if (!modelData.thumbnail.empty()) {
      sqlite3_bind_blob(stmt, 5, modelData.thumbnail.data(),
                        static_cast<int>(modelData.thumbnail.size()),
                        SQLITE_TRANSIENT);
    } else {
      sqlite3_bind_null(stmt, 5);
    }

    sqlite3_bind_text(stmt, 6, modelData.author.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, modelData.file_path.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, modelData.library_name.c_str(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 9, modelData.is_selected ? 1 : 0);
    sqlite3_bind_int(stmt, 10, modelData.is_processed ? 1 : 0);
    sqlite3_bind_int(stmt, 11, modelData.is_included ? 1 : 0);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
      std::cerr << "Insert model failed: " << sqlite3_errmsg(db) << std::endl;
      sqlite3_finalize(stmt);
      return false;
    }
    int id = static_cast<int>(sqlite3_last_insert_rowid(db));
    sqlite3_finalize(stmt);

    ModelData modelDataWithId = modelData;
    modelDataWithId.id = id;
    modelDataWithId.short_name = short_name;

    beginInsertRows(QModelIndex(), models.size(), models.size());
    models.push_back(modelDataWithId);
    endInsertRows();

    qDebug() << "Model inserted successfully with id:" << id
             << ", short_name:" << QString::fromStdString(short_name);

    return true;
  } else {
    std::cerr << "SQL error in insertModel: " << sqlite3_errmsg(db)
              << std::endl;
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
      qDebug() << "shortNameExists - count for"
               << QString::fromStdString(short_name) << ":" << count;
    }
    sqlite3_finalize(stmt);
  } else {
    std::cerr << "SQL error in shortNameExists: " << sqlite3_errmsg(db)
              << std::endl;
  }

  return count > 0;
}

bool Model::filePathExists(const std::string& file_path) {
  std::string sql = "SELECT COUNT(*) FROM models WHERE file_path = ?;";
  sqlite3_stmt* stmt;
  int count = 0;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);

  qDebug() << "Checking if file_path exists:"
           << QString::fromStdString(file_path);

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, file_path.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
      count = sqlite3_column_int(stmt, 0);
      qDebug() << "filePathExists - count for"
               << QString::fromStdString(file_path) << ":" << count;
    }
    sqlite3_finalize(stmt);
  } else {
    std::cerr << "SQL error in filePathExists: " << sqlite3_errmsg(db)
              << std::endl;
  }

  return count > 0;
}

bool Model::updateModel(int id, const ModelData& modelData) {
  std::string sql = R"(
        UPDATE models SET
            short_name = ?,
            primary_file = ?,
            override_info = ?,
            title = ?,
            thumbnail = ?,
            author = ?,
            file_path = ?,
            library_name = ?,
            is_selected = ?,
            is_processed = ?,
            is_included = ?
        WHERE id = ?;
    )";

  // Remove existing tags for the model
  std::string sqlDeleteTags = "DELETE FROM model_tags WHERE model_id = ?;";
  sqlite3_stmt* deleteStmt;
  if (sqlite3_prepare_v2(db, sqlDeleteTags.c_str(), -1, &deleteStmt, nullptr) ==
      SQLITE_OK) {
    sqlite3_bind_int(deleteStmt, 1, id);
    if (sqlite3_step(deleteStmt) != SQLITE_DONE) {
      std::cerr << "Failed to delete existing tags: " << sqlite3_errmsg(db)
                << std::endl;
    }
    sqlite3_finalize(deleteStmt);
  } else {
    std::cerr << "SQL error in delete existing tags: " << sqlite3_errmsg(db)
              << std::endl;
  }

  // Insert new tags for the model
  for (const auto& tag : modelData.tags) {
    addTagToModel(id, tag);
  }

  sqlite3_stmt* stmt;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);

  // Ensure short_name is unique if it's changed
  std::string short_name = modelData.short_name;
  int suffix = 1;
  while (shortNameExists(short_name) &&
         getModelById(id).short_name != short_name) {
    short_name = modelData.short_name + "_" + std::to_string(suffix++);
  }

  // Ensure file_path is unique if it's changed
  if (filePathExists(modelData.file_path) &&
      getModelById(id).file_path != modelData.file_path) {
    std::cerr << "Another model with file_path " << modelData.file_path
              << " already exists." << std::endl;
    return false;
  }

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, short_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, modelData.primary_file.c_str(), -1,
                      SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, modelData.override_info.c_str(), -1,
                      SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, modelData.title.c_str(), -1, SQLITE_STATIC);

    if (!modelData.thumbnail.empty()) {
      sqlite3_bind_blob(stmt, 5, modelData.thumbnail.data(),
                        static_cast<int>(modelData.thumbnail.size()),
                        SQLITE_STATIC);
    } else {
      sqlite3_bind_null(stmt, 5);
    }

    sqlite3_bind_text(stmt, 6, modelData.author.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, modelData.file_path.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 8, modelData.library_name.c_str(), -1,
                      SQLITE_STATIC);
    sqlite3_bind_int(stmt, 9, modelData.is_selected ? 1 : 0);
    sqlite3_bind_int(stmt, 10, modelData.is_processed ? 1 : 0);
    sqlite3_bind_int(stmt, 11, modelData.is_included ? 1 : 0);
    sqlite3_bind_int(stmt, 12, id);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
      std::cerr << "Update model failed: " << sqlite3_errmsg(db) << std::endl;
      sqlite3_finalize(stmt);
      return false;
    }
    sqlite3_finalize(stmt);

    // Update the models vector
    for (int row = 0; row < static_cast<int>(models.size()); ++row) {
      if (models[row].id == id) {
        models[row] = modelData;
        models[row].short_name = short_name;
        QModelIndex modelIndex = index(row);
        emit dataChanged(modelIndex, modelIndex);
        break;
      }
    }

    return true;
  } else {
    std::cerr << "SQL error in updateModel: " << sqlite3_errmsg(db)
              << std::endl;
    return false;
  }
}

bool Model::deleteModel(int id) {
  // First, delete associated objects
  if (!deleteObjectsForModel(id)) {
    return false;
  }

  std::string sql = "DELETE FROM models WHERE id = ?;";
  sqlite3_stmt* stmt;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    sqlite3_bind_int(stmt, 1, id);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
      std::cerr << "Delete model failed: " << sqlite3_errmsg(db) << std::endl;
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
    std::cerr << "SQL error in deleteModel: " << sqlite3_errmsg(db)
              << std::endl;
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
    std::cerr << "SQL error in modelExists: " << sqlite3_errmsg(db)
              << std::endl;
  }

  return count > 0;
}

ModelData Model::getModelById(int id) {
  std::string sql = R"(
        SELECT id, short_name, primary_file, override_info, title, thumbnail, author, file_path, library_name, is_selected, is_processed, is_included
        FROM models WHERE id = ?;
    )";
  sqlite3_stmt* stmt;
  ModelData model;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    sqlite3_bind_int(stmt, 1, id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      model.id = sqlite3_column_int(stmt, 0);
      model.short_name =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
      model.primary_file =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
      model.override_info =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
      model.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));

      const void* blob = sqlite3_column_blob(stmt, 5);
      int blob_size = sqlite3_column_bytes(stmt, 5);
      if (blob && blob_size > 0) {
        model.thumbnail.assign(static_cast<const char*>(blob),
                               static_cast<const char*>(blob) + blob_size);
      }

      model.author =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
      model.file_path =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
      model.library_name =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
      model.is_selected = sqlite3_column_int(stmt, 9) != 0;
      model.is_processed = sqlite3_column_int(stmt, 10) != 0;
      model.is_included = sqlite3_column_int(stmt, 11) != 0;
    }
    sqlite3_finalize(stmt);
  } else {
    std::cerr << "Failed to select model: " << sqlite3_errmsg(db) << std::endl;
  }

  return model;
}

ModelData Model::getModelByFilePath(const std::string& filePath) {
  ModelData model;
  model.id = 0;
  std::string sql = R"(
        SELECT id, short_name, primary_file, override_info, title, thumbnail,
               author, file_path, library_name, is_selected, is_processed, is_included
        FROM models WHERE file_path = ?;
    )";
  sqlite3_stmt* stmt;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    // Use SQLITE_TRANSIENT to ensure SQLite makes its own copy of the data
    sqlite3_bind_text(stmt, 1, filePath.c_str(), -1, SQLITE_TRANSIENT);

    // Debugging statements
    // qDebug() << "Executing SQL:" << QString::fromStdString(sql);
    // qDebug() << "With filePath:" << QString::fromStdString(filePath);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
      model.id = sqlite3_column_int(stmt, 0);

      const char* text;

      text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
      model.short_name = text ? text : "";

      text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
      model.primary_file = text ? text : "";

      text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
      model.override_info = text ? text : "";

      text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
      model.title = text ? text : "";

      const void* blob = sqlite3_column_blob(stmt, 5);
      int blob_size = sqlite3_column_bytes(stmt, 5);
      if (blob && blob_size > 0) {
        model.thumbnail.assign(static_cast<const char*>(blob),
                               static_cast<const char*>(blob) + blob_size);
      } else {
        model.thumbnail.clear();
      }

      text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
      model.author = text ? text : "";

      text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
      model.file_path = text ? text : "";

      text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
      model.library_name = text ? text : "";

      model.is_selected = sqlite3_column_int(stmt, 9) != 0;
      model.is_processed = sqlite3_column_int(stmt, 10) != 0;
      model.is_included = sqlite3_column_int(stmt, 11) != 0;

      qDebug() << "Model found with id:" << model.id
               << "and filePath:" << QString::fromStdString(model.file_path);
    } else {
      qDebug() << "No model found with filePath:"
               << QString::fromStdString(filePath);
    }

    sqlite3_finalize(stmt);
  } else {
    std::cerr << "Failed to select model by file path: " << sqlite3_errmsg(db)
              << std::endl;
  }

  return model;
}

void Model::loadModelsFromDatabase() {
  std::vector<ModelData> loadedModels;
  std::string sql = R"(
        SELECT id, short_name, primary_file, override_info, title, thumbnail, author, file_path, library_name, is_selected, is_processed, is_included
        FROM models;
    )";
  sqlite3_stmt* stmt;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      ModelData model;
      model.id = sqlite3_column_int(stmt, 0);
      model.short_name =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
      model.primary_file =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
      model.override_info =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
      model.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));

      const void* blob = sqlite3_column_blob(stmt, 5);
      int blob_size = sqlite3_column_bytes(stmt, 5);
      if (blob && blob_size > 0) {
        model.thumbnail.assign(static_cast<const char*>(blob),
                               static_cast<const char*>(blob) + blob_size);
      }

      model.author =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
      model.file_path =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
      model.library_name =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
      model.is_selected = sqlite3_column_int(stmt, 9) != 0;
      model.is_processed = sqlite3_column_int(stmt, 10) != 0;
      model.is_included = sqlite3_column_int(stmt, 11) != 0;

      loadedModels.push_back(model);
    }
    sqlite3_finalize(stmt);

    // Update the models vector
    beginResetModel();
    models = std::move(loadedModels);
    endResetModel();

  } else {
    std::cerr << "Failed to select models: " << sqlite3_errmsg(db) << std::endl;
  }
}

int Model::hashModel(const std::string& modelDir) {
  qDebug() << "hashModel called with modelDir:"
           << QString::fromStdString(modelDir);

  std::ifstream file(modelDir, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "Could not open file for hashing: " << modelDir << std::endl;
    return 0;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string fileContents = buffer.str();

  std::hash<std::string> hasher;
  int hashValue = static_cast<int>(hasher(fileContents));

  qDebug() << "Hash value for" << QString::fromStdString(modelDir) << ":"
           << hashValue;

  return hashValue;
}

void Model::printModel(const ModelData& modelData) {
  std::cout << "Model ID: " << modelData.id << std::endl;
  std::cout << "Short Name: " << modelData.short_name << std::endl;
  std::cout << "Primary File: " << modelData.primary_file << std::endl;
  std::cout << "Override Info: " << modelData.override_info << std::endl;
  std::cout << "Title: " << modelData.title << std::endl;
  std::cout << "Author: " << modelData.author << std::endl;
  std::cout << "File Path: " << modelData.file_path << std::endl;
  std::cout << "Library Name: " << modelData.library_name << std::endl;
  std::cout << "Is Selected: " << (modelData.is_selected ? "Yes" : "No")
            << std::endl;
  std::cout << "Thumbnail Size: " << modelData.thumbnail.size() << " bytes"
            << std::endl;
}

std::string Model::getHiddenDirectoryPath() const { return hiddenDirPath; }

bool Model::executeSQL(const std::string& sql) {
  char* errMsg = nullptr;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
  if (rc != SQLITE_OK) {
    std::cerr << "SQL error in executeSQL: " << errMsg << std::endl;
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

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
      sqlite3_bind_int(stmt, 1, modelData.is_selected ? 1 : 0);
      sqlite3_bind_int(stmt, 2, modelData.id);

      if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Failed to update is_selected in database: "
                  << sqlite3_errmsg(db) << std::endl;
        sqlite3_finalize(stmt);
        return false;
      }
      sqlite3_finalize(stmt);
    } else {
      std::cerr << "SQL error in setData when updating is_selected: "
                << sqlite3_errmsg(db) << std::endl;
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
        std::cerr << "Failed to update is_included in database: "
                  << sqlite3_errmsg(db) << std::endl;
        sqlite3_finalize(stmt);
        return false;
      }
      sqlite3_finalize(stmt);
    } else {
      std::cerr << "SQL error in setData when updating is_included: "
                << sqlite3_errmsg(db) << std::endl;
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
  std::string sql = R"(
        INSERT INTO objects (model_id, name, parent_object_id, is_selected)
        VALUES (?, ?, ?, ?);
    )";

  sqlite3_stmt* stmt;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    std::cerr << "SQL error in insertObject: " << sqlite3_errmsg(db)
              << std::endl;
    return -1;
  }

  sqlite3_bind_int(stmt, 1, obj.model_id);
  sqlite3_bind_text(stmt, 2, obj.name.c_str(), -1, SQLITE_STATIC);

  if (obj.parent_object_id != -1) {
    sqlite3_bind_int(stmt, 3, obj.parent_object_id);
  } else {
    sqlite3_bind_null(stmt, 3);
  }

  sqlite3_bind_int(stmt, 4, obj.is_selected ? 1 : 0);

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    std::cerr << "Insert object failed: " << sqlite3_errmsg(db) << std::endl;
    sqlite3_finalize(stmt);
    return -1;
  }

  int object_id = static_cast<int>(sqlite3_last_insert_rowid(db));
  sqlite3_finalize(stmt);
  return object_id;
}

bool Model::deleteObjectsForModel(int model_id) {
  std::string sql = "DELETE FROM objects WHERE model_id = ?;";
  sqlite3_stmt* stmt;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    sqlite3_bind_int(stmt, 1, model_id);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
      std::cerr << "Delete objects failed: " << sqlite3_errmsg(db) << std::endl;
      sqlite3_finalize(stmt);
      return false;
    }
    sqlite3_finalize(stmt);
    return true;
  } else {
    std::cerr << "SQL error in deleteObjectsForModel: " << sqlite3_errmsg(db)
              << std::endl;
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
    std::cerr << "Failed to retrieve objects: " << sqlite3_errmsg(db)
              << std::endl;
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

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    std::cerr << "SQL error in updateObjectSelection: " << sqlite3_errmsg(db)
              << std::endl;
    return false;
  }

  sqlite3_bind_int(stmt, 1, is_selected ? 1 : 0);
  sqlite3_bind_int(stmt, 2, object_id);

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    std::cerr << "Update object selection failed: " << sqlite3_errmsg(db)
              << std::endl;
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

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    std::cerr << "SQL error in updateObject: " << sqlite3_errmsg(db)
              << std::endl;
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
    std::cerr << "Update object failed: " << sqlite3_errmsg(db) << std::endl;
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
      std::cerr << "Object with ID " << object_id << " not found." << std::endl;
    }

    sqlite3_finalize(stmt);
  } else {
    std::cerr << "SQL error in getObjectById: " << sqlite3_errmsg(db)
              << std::endl;
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
    std::cerr << "Failed to prepare statement in getSelectedObjectsForModel: "
              << sqlite3_errmsg(db) << std::endl;
  }

  return selectedObjects;
}

void Model::beginTransaction() { executeSQL("BEGIN TRANSACTION;"); }

void Model::commitTransaction() { executeSQL("COMMIT;"); }

bool Model::updateObjectParentId(int object_id, int parent_object_id) {
  std::string sql =
      "UPDATE objects SET parent_object_id = ? WHERE object_id = ?;";
  sqlite3_stmt* stmt;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    std::cerr << "SQL error in updateObjectParentId: " << sqlite3_errmsg(db)
              << std::endl;
    return false;
  }

  sqlite3_bind_int(stmt, 1, parent_object_id);
  sqlite3_bind_int(stmt, 2, object_id);

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    std::cerr << "Update object parent ID failed: " << sqlite3_errmsg(db)
              << std::endl;
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
    std::cerr << "Failed to delete tables." << std::endl;
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

  std::cout << "Removing tag " << tagName << " from model " << modelId
            << std::endl;

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
  std::string sql = R"(
    SELECT short_name, primary_file, override_info, title, thumbnail, author, file_path, library_name
    FROM models
    WHERE id = ?;
  )";
  sqlite3_stmt* stmt = prepareStatement(sql);
  if (!stmt) return properties;
  sqlite3_bind_int(stmt, 1, modelId);

  int columnCount = sqlite3_column_count(stmt);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    for (int i = 0; i < columnCount; ++i) {
      const char* columnName = sqlite3_column_name(stmt, i);
      const char* columnValue =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
      if (columnValue) {
        properties[columnName] = columnValue;
      } else if (sqlite3_column_type(stmt, i) == SQLITE_BLOB) {
        properties[columnName] = std::string(
            reinterpret_cast<const char*>(sqlite3_column_blob(stmt, i)),
            sqlite3_column_bytes(stmt, i));
      } else {
        properties[columnName] = "";
      }
    }
  } else {
    std::cerr << "Failed to retrieve properties for model: "
              << sqlite3_errmsg(db) << std::endl;
  }

  std::vector<std::string> columns = {"short_name", "title", "author",
                                      "file_path", "library_name"};

  for (auto it = properties.begin(); it != properties.end();)
    if (std::find(columns.begin(), columns.end(), it->first) == columns.end())
      it = properties.erase(it);
    else
      ++it;

  sqlite3_finalize(stmt);
  return properties;
}

bool Model::setPropertyForModel(int modelId, const std::string& property,
                                const std::string& value) {
  // Validate that the property is an allowed column
  static const std::set<std::string> allowedProperties = {
      "short_name", "primary_file", "override_info", "title",
      "author",     "file_path",    "library_name"};

  if (allowedProperties.find(property) == allowedProperties.end()) {
    std::cerr << "Invalid property name: " << property << std::endl;
    return false;
  }

  std::string sql = "UPDATE models SET " + property + " = ? WHERE id = ?;";
  sqlite3_stmt* stmt = prepareStatement(sql);
  if (!stmt) return false;

  sqlite3_bind_text(stmt, 1, value.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, modelId);

  return executePreparedStatement(stmt);
}

// Simplifying executions
sqlite3_stmt* Model::prepareStatement(const std::string& sql) const {
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db)
              << std::endl;
    return nullptr;
  }
  return stmt;
}

bool Model::executePreparedStatement(sqlite3_stmt* stmt) {
  std::lock_guard<std::recursive_mutex> lock(db_mutex);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    std::cerr << "Execution failed: " << sqlite3_errmsg(db) << std::endl;
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
               author, file_path, library_name, is_selected, is_processed, is_included
        FROM models
        WHERE is_included = 1;
    )";
  sqlite3_stmt* stmt;
  std::lock_guard<std::recursive_mutex> lock(db_mutex);

  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      ModelData model;
      model.id = sqlite3_column_int(stmt, 0);
      model.short_name =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
      model.primary_file =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
      model.override_info =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
      model.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));

      const void* blob = sqlite3_column_blob(stmt, 5);
      int blob_size = sqlite3_column_bytes(stmt, 5);
      if (blob && blob_size > 0) {
        model.thumbnail.assign(static_cast<const char*>(blob),
                               static_cast<const char*>(blob) + blob_size);
      }

      model.author =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
      model.file_path =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
      model.library_name =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
      model.is_selected = sqlite3_column_int(stmt, 9) != 0;
      model.is_processed = sqlite3_column_int(stmt, 10) != 0;
      model.is_included = sqlite3_column_int(stmt, 11) != 0;

      includedModels.push_back(model);
    }
    sqlite3_finalize(stmt);
  } else {
    std::cerr << "Failed to select included models: " << sqlite3_errmsg(db)
              << std::endl;
  }

  return includedModels;
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
    std::cerr << "SQL error in isFileIncluded: " << sqlite3_errmsg(db)
              << std::endl;
  }

  return included;
}


std::vector<ModelData> Model::getIncludedNotProcessedModels() {
    std::vector<ModelData> notProcessedModels;

    const char* sql = R"(
        SELECT id, short_name, primary_file, override_info, title,
               thumbnail, author, file_path, library_name, is_selected,
               is_processed, is_included
        FROM models
        WHERE is_included = 1 AND is_processed = 0;
    )";

    sqlite3_stmt* stmt;
    std::lock_guard<std::recursive_mutex> lock(db_mutex);

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ModelData modelData;
            modelData.id = sqlite3_column_int(stmt, 0);
            modelData.short_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            modelData.primary_file = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            modelData.override_info = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            modelData.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));

            const void* thumbnailBlob = sqlite3_column_blob(stmt, 5);
            int thumbnailSize = sqlite3_column_bytes(stmt, 5);
            if (thumbnailBlob && thumbnailSize > 0) {
                const char* blobPtr = static_cast<const char*>(thumbnailBlob);
                modelData.thumbnail.assign(blobPtr, blobPtr + thumbnailSize);
            }

            modelData.author = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            modelData.file_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            modelData.library_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            modelData.is_selected = sqlite3_column_int(stmt, 9) == 1;
            modelData.is_processed = sqlite3_column_int(stmt, 10) == 1;
            modelData.is_included = sqlite3_column_int(stmt, 11) == 1;

            notProcessedModels.push_back(modelData);
        }

        sqlite3_finalize(stmt);
    } else {
        std::cerr << "[Model::getIncludedNotProcessedModels] SQL error: "
                  << sqlite3_errmsg(db) << std::endl;
    }

    return notProcessedModels;
}
