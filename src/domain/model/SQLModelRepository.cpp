#include "SQLModelRepository.h"
#include "Logger.h"

#include <filesystem>
#include <sstream>
#include <optional>
#include <unordered_set>

using namespace std::string_literals;

SQLModelRepository::SQLModelRepository(std::string dbPath) : m_db(dbPath) {
    (void)createTables();
}

bool SQLModelRepository::createTables() {
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
            FOREIGN KEY(model_id) REFERENCES models(id) ON DELETE CASCADE,
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

    return m_db.exec(sqlModels) &&
           m_db.exec(sqlObjects) &&
           m_db.exec(sqlTags)   &&
           m_db.exec(sqlModelTags) &&
           m_db.exec("CREATE INDEX IF NOT EXISTS idx_models_included ON models(is_included);") &&
           m_db.exec("CREATE INDEX IF NOT EXISTS idx_models_inc_proc ON models(is_included, is_processed);");
}

bool SQLModelRepository::reset() {
    // delete tables, then recreate
    const char* sqlDeleteModelTags = "DROP TABLE IF EXISTS model_tags;";
    const char* sqlDeleteTags      = "DROP TABLE IF EXISTS tags;";
    const char* sqlDeleteObjects   = "DROP TABLE IF EXISTS objects;";
    const char* sqlDeleteModels    = "DROP TABLE IF EXISTS models;";

    return m_db.exec(sqlDeleteModelTags) && 
           m_db.exec(sqlDeleteTags) && 
           m_db.exec(sqlDeleteObjects) && 
           m_db.exec(sqlDeleteModels) && 
           createTables();
}

ModelData SQLModelRepository::readModelRow(sqlite3_stmt* stmt) {
    auto getText = [&](int col) -> std::string {
        const unsigned char* text = sqlite3_column_text(stmt, col);
        return text ? reinterpret_cast<const char*>(text) : std::string{};
    };

    ModelData md;
    md.id            = sqlite3_column_int(stmt, 0);
    md.short_name    = getText(1);
    md.primary_file  = getText(2);
    md.override_info = getText(3);
    md.title         = getText(4);

    const void* blob     = sqlite3_column_blob(stmt, 5);
    const int blob_size  = sqlite3_column_bytes(stmt, 5);
    if (blob && blob_size > 0) {
        const char* blob_cast = static_cast<const char*>(blob);
        md.thumbnail.assign(blob_cast, blob_cast + blob_size);
    } else {
        md.thumbnail.clear();
    }

    md.author        = getText(6);
    md.file_path     = getText(7);
    md.library_name  = getText(8);
    md.is_selected   = sqlite3_column_int(stmt, 9)  != 0;
    md.is_processed  = sqlite3_column_int(stmt, 10) != 0;
    md.is_included   = sqlite3_column_int(stmt, 11) != 0;

    return md;
}

std::vector<ModelData> SQLModelRepository::getAllModels() const {
    return getAllModelsHeavy(false, false);
}

std::vector<ModelData> SQLModelRepository::getAllModelsHeavy(bool with_tags, bool with_objects) const {
    std::vector<ModelData> out;

    // load all models
    static const char* SQL_MODELS = R"(
        SELECT id, short_name, primary_file, override_info, title,
               thumbnail, author, file_path, library_name,
               is_selected, is_processed, is_included
        FROM models
        ORDER BY id;
    )";

    m_db.query(SQL_MODELS,
        [&](sqlite3_stmt* st) {
            out.push_back(readModelRow(st));
            return true;
        }
    );

    if (out.empty() || (!with_tags && !with_objects)) {
        return out;
    }

    // build id -> index map for faster lookup
    std::unordered_map<int, std::size_t> idx_by_id;
    idx_by_id.reserve(out.size());
    for (std::size_t i = 0; i < out.size(); ++i) {
        idx_by_id[out[i].id] = i;
    }

    // fill in tag data
    if (with_tags) {
        static const char* SQL_TAGS = R"(
            SELECT mt.model_id, t.name
            FROM model_tags mt
            JOIN tags t ON t.id = mt.tag_id
            ORDER BY mt.model_id, t.name;
        )";
        m_db.query(SQL_TAGS,
            [&](sqlite3_stmt* st) {
                const int model_id = sqlite3_column_int(st, 0);
                const unsigned char* tags_text = sqlite3_column_text(st, 1);
                if (tags_text) {
                    auto it = idx_by_id.find(model_id);
                    if (it != idx_by_id.end()) {
                        out[it->second].tags.emplace_back(reinterpret_cast<const char*>(tags_text));
                    }
                }
                return true;
            }
        );
    }

    // fill in objects data
    if (with_objects) {
        static const char* SQL_OBJS = R"(
            SELECT object_id, model_id, name, parent_object_id, is_selected
            FROM objects
            ORDER BY model_id, object_id;
        )";
        m_db.query(SQL_OBJS,
            [&](sqlite3_stmt* st) {
                ObjectData row;
                row.object_id = sqlite3_column_int(st, 0);
                row.model_id  = sqlite3_column_int(st, 1);

                if (const unsigned char* name = sqlite3_column_text(st, 2)) {
                    row.name = reinterpret_cast<const char*>(name);
                }

                if (sqlite3_column_type(st, 3) == SQLITE_NULL) {
                    row.parent_object_id = -1;
                } else {
                    row.parent_object_id = sqlite3_column_int(st, 3);
                }

                row.is_selected = sqlite3_column_int(st, 4) != 0;

                auto it = idx_by_id.find(row.model_id);
                if (it != idx_by_id.end()) {
                    out[it->second].objects.emplace_back(std::move(row));
                }
                return true;
            }
        );
    }

    return out;
}

std::optional<ModelData> SQLModelRepository::getModelById(int id) const {
    std::optional<ModelData> out;

    static const char* SQL = R"(
        SELECT id, short_name, primary_file, override_info, title,
               thumbnail, author, file_path, library_name,
               is_selected, is_processed, is_included
        FROM models WHERE id = ?1;
    )";

    m_db.query(SQL,
        [&](sqlite3_stmt* st) { sqlite3_bind_int(st, 1, id); },
        [&](sqlite3_stmt* st) {
            out = readModelRow(st);
            return false;   // return after first row
        });

    return out;
}

std::optional<ModelData> SQLModelRepository::getModelByFilePath(std::string filePath) const {
    // normalize path
    std::string generic = std::filesystem::path(filePath).generic_string();
    std::optional<ModelData> out;

    static const char* SQL = R"(
        SELECT id, short_name, primary_file, override_info, title,
               thumbnail, author, file_path, library_name,
               is_selected, is_processed, is_included
        FROM models WHERE file_path = ?1;
    )";

    m_db.query(SQL,
        [&](sqlite3_stmt* st) { sqlite3_bind_text(st, 1, generic.c_str(), -1, SQLITE_TRANSIENT); },
        [&](sqlite3_stmt* st) {
            out = readModelRow(st);
            return false;   // return after first row
        });

    return out;
}

std::vector<unsigned char> SQLModelRepository::getThumbnail(int modelId) const {
    static const char* SQL = "SELECT thumbnail FROM models WHERE id = ?1;";

    return m_db.readBlob(SQL,
        [&](sqlite3_stmt* st){ sqlite3_bind_int(st, 1, modelId); });
}

std::vector<std::string> SQLModelRepository::getTagsForModel(int modelId) const {
    std::vector<std::string> tags;
    static const char* SQL = R"(
        SELECT name
        FROM tags t 
        JOIN model_tags mt ON t.id = mt.tag_id WHERE mt.model_id = ?1
        ORDER BY name;
    )";

    m_db.query(SQL,
        [&](sqlite3_stmt* st) { sqlite3_bind_int(st, 1, modelId); },
        [&](sqlite3_stmt* st) {
            const unsigned char* txt = sqlite3_column_text(st, 0);
            if (txt)
                tags.emplace_back(reinterpret_cast<const char*>(txt));
            return true;
        });

    return tags;
}

bool SQLModelRepository::isFileIncluded(std::string filePath) const {
    // normalize path
    std::string generic = std::filesystem::path(filePath).generic_string();

    static const char* SQL = "SELECT is_included FROM models WHERE file_path = ?1;";
    bool included =  false;
    m_db.query(SQL,
        [&](sqlite3_stmt* st) { sqlite3_bind_text(st, 1, generic.c_str(), -1, SQLITE_TRANSIENT); },
        [&](sqlite3_stmt* st) {
            included = sqlite3_column_int(st, 0) != 0;
            return false;   // return after first row
        });

    return included;
}

std::vector<ModelData> SQLModelRepository::getIncludedModels() const {
    std::vector<ModelData> out;
    static const char* SQL = R"(
        SELECT id, short_name, primary_file, override_info, title,
               thumbnail, author, file_path, library_name,
               is_selected, is_processed, is_included
        FROM models WHERE is_included = 1
        ORDER BY id;
    )";

    m_db.query(SQL,
        [&](sqlite3_stmt* st) {
            out.push_back(readModelRow(st));
            return true;
        });

    return out;
}

std::vector<ModelData> SQLModelRepository::getIncludedNotProcessedModels() const {
    std::vector<ModelData> out;
    static const char* SQL = R"(
        SELECT id, short_name, primary_file, override_info, title,
               thumbnail, author, file_path, library_name,
               is_selected, is_processed, is_included
        FROM models
        WHERE is_included = 1 AND is_processed = 0;
    )";

    m_db.query(SQL,
        [&](sqlite3_stmt* st) {
            out.push_back(readModelRow(st));
            return true;
        });

    return out;
}

bool SQLModelRepository::shortNameExists(std::string_view short_name) const {
    static const char* SQL = "SELECT COUNT(*) FROM models WHERE short_name = ?1;";

    int cnt = 0;
    m_db.query(SQL,
        [&](sqlite3_stmt* st) { sqlite3_bind_text(st, 1, std::string(short_name).c_str(), -1, SQLITE_TRANSIENT); },
        [&](sqlite3_stmt* st) {
            cnt = sqlite3_column_int(st, 0);
            return false;   // return after first row
        });
    return cnt > 0;
}

bool SQLModelRepository::filePathExists(std::string_view file_path) const {
    static const char* SQL = "SELECT COUNT(*) FROM models WHERE file_path = ?1;";

    int cnt = 0;
    m_db.query(SQL,
        [&](sqlite3_stmt* st) { sqlite3_bind_text(st, 1, std::string(file_path).c_str(), -1, SQLITE_TRANSIENT); },
        [&](sqlite3_stmt* st) {
            cnt = sqlite3_column_int(st, 0);
            return false;   // return after first row
        });

    return cnt > 0;
}

std::string SQLModelRepository::makeUniqueShortName(std::string base) const {
    static const char* SQL = R"(
        SELECT short_name
        FROM models
        WHERE short_name = ?1
           OR short_name LIKE (?1 || '_%');
    )";

    // get all potential conflicts upfront
    std::unordered_set<std::string> existing;
    m_db.query(SQL,
        [&](sqlite3_stmt* st) { sqlite3_bind_text(st, 1, base.c_str(), -1, SQLITE_TRANSIENT); },
        [&](sqlite3_stmt* st) {
            const unsigned char* txt = sqlite3_column_text(st, 0);
            if (txt)
                existing.emplace(reinterpret_cast<const char*>(txt));
            return true;
        });

    // find a candidate
    constexpr int maxSuffix = 1000;
    for (int s = 0; s <= maxSuffix; ++s) {
        std::string cand = (s == 0) ? base : (base + "_" + std::to_string(s));
        if (existing.find(cand) == existing.end())
            return cand;    // good name
    }
    return {};  // exceeded max suffix - give up
}

std::optional<ModelData> SQLModelRepository::insertModel(const ModelData& md) {
    const std::string generic_filepath = std::filesystem::path(md.file_path).generic_string();

    if (filePathExists(generic_filepath)) {
        LOG_INFO << "Model with file_path " << generic_filepath << " already exists." << LOG_ENDL;
        return std::nullopt;
    }

    std::string short_name = makeUniqueShortName(md.short_name);
    if (short_name.empty()) {
        LOG_ERR << "Could not generate unique short_name for " << md.short_name << LOG_ENDL;
        return std::nullopt;
    }

    static const char* SQL = R"(
        INSERT INTO models
          (short_name, primary_file, override_info, title, thumbnail, author,
           file_path, library_name, is_selected, is_processed, is_included)
        VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11)
        RETURNING id;
    )";

    int new_id = 0;
    const bool ok = m_db.exec(
        SQL,
        [&](sqlite3_stmt* st) {
            sqlite3_bind_text(st, 1,  short_name.c_str(),       -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 2,  md.primary_file.c_str(),  -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 3,  md.override_info.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 4,  md.title.c_str(),         -1, SQLITE_TRANSIENT);
            if (!md.thumbnail.empty())
                sqlite3_bind_blob(st, 5, md.thumbnail.data(), (int)md.thumbnail.size(), SQLITE_TRANSIENT);
            else
                sqlite3_bind_null(st, 5);
            sqlite3_bind_text(st, 6,  md.author.c_str(),        -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 7,  generic_filepath.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 8,  md.library_name.c_str(),  -1, SQLITE_TRANSIENT);
            sqlite3_bind_int (st, 9,  md.is_selected  ? 1 : 0);
            sqlite3_bind_int (st, 10, md.is_processed ? 1 : 0);
            sqlite3_bind_int (st, 11, md.is_included  ? 1 : 0);
        },
        [&](sqlite3_stmt* st) {
            new_id = sqlite3_column_int(st, 0);
            return false;
        }
    );

    if (!ok || new_id <= 0)
        return std::nullopt;

    // update return modelData
    ModelData out = md;
    out.id         = new_id;
    out.file_path  = generic_filepath;
    out.short_name = short_name;

    return out;
}

bool SQLModelRepository::updateModel(int id, const ModelData& md) {
    // TODO: can we get rid of this extra lookup
    auto existing = getModelById(id);
    if (!existing)
        return false;

    // ensure unique filepath
    std::string filePath = existing->file_path;
    if (!md.file_path.empty()) {
        std::string generic = std::filesystem::path(md.file_path).generic_string();
        if (generic != existing->file_path && filePathExists(generic)) {
            LOG_ERR << "Another model already uses file_path " << generic << LOG_ENDL;
            return false;
        }
        filePath = std::move(generic);
    }

    // ensure unique shortname
    std::string short_name = md.short_name.empty() ? existing->short_name : md.short_name;
    if (short_name != existing->short_name && shortNameExists(short_name)) {
        short_name = makeUniqueShortName(md.short_name);
        if (short_name.empty()) return false;
    }

    static const char* SQL = R"(
        UPDATE models SET
            short_name   = ?1,
            primary_file = ?2,
            override_info= ?3,
            title        = ?4,
            thumbnail    = ?5,
            author       = ?6,
            file_path    = ?7,
            library_name = ?8,
            is_selected  = ?9,
            is_processed = ?10,
            is_included  = ?11
        WHERE id = ?12;
    )";

    return m_db.exec(SQL, [&](sqlite3_stmt* st) {
        sqlite3_bind_text(st, 1,  short_name.c_str(),       -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2,  md.primary_file.c_str(),  -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3,  md.override_info.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4,  md.title.c_str(),         -1, SQLITE_TRANSIENT);
        if (!md.thumbnail.empty())
            sqlite3_bind_blob(st, 5, md.thumbnail.data(), (int)md.thumbnail.size(), SQLITE_TRANSIENT);
        else
            sqlite3_bind_null(st, 5);
        sqlite3_bind_text(st, 6,  md.author.c_str(),        -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 7,  filePath.c_str(),         -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 8,  md.library_name.c_str(),  -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (st, 9,  md.is_selected  ? 1 : 0);
        sqlite3_bind_int (st, 10, md.is_processed ? 1 : 0);
        sqlite3_bind_int (st, 11, md.is_included  ? 1 : 0);
        sqlite3_bind_int (st, 12, id);
    });
}

bool SQLModelRepository::deleteModel(int modelId) {
    // delete should cascade delete objects + tags
    static const char* SQL = "DELETE FROM models WHERE id = ?1;";
    return m_db.exec(SQL, [&](sqlite3_stmt* st) { sqlite3_bind_int(st, 1, modelId); });
}

bool SQLModelRepository::setModelIncluded(int modelId, bool is_included) {
    static const char* SQL =
        "UPDATE models SET is_included = ?1 WHERE id = ?2 AND is_included <> ?1;";
    return m_db.exec(SQL, [&](sqlite3_stmt* st){
        sqlite3_bind_int(st, 1, is_included ? 1 : 0);
        sqlite3_bind_int(st, 2, modelId);
    });
}

bool SQLModelRepository::setModelProcessed(int modelId, bool is_processed) {
    static const char* SQL = "UPDATE models SET is_processed = ?1 WHERE id = ?2;";
    return m_db.exec(SQL, [&](sqlite3_stmt* st){
        sqlite3_bind_int(st, 1, is_processed ? 1 : 0);
        sqlite3_bind_int(st, 2, modelId);
    });
}

bool SQLModelRepository::setModelSelected(int modelId, bool is_selected) {
    static const char* SQL = "UPDATE models SET is_selected = ?1 WHERE id = ?2;";
    return m_db.exec(SQL, [&](sqlite3_stmt* st){
        sqlite3_bind_int(st, 1, is_selected ? 1 : 0);
        sqlite3_bind_int(st, 2, modelId);
    });
}

bool SQLModelRepository::setModelThumbnail(int modelId, const std::vector<unsigned char>& png) {
    static const char* SQL = "UPDATE models SET thumbnail = ?1 WHERE id = ?2;";
    return m_db.exec(SQL, [&](sqlite3_stmt* st){
        if (!png.empty())
            sqlite3_bind_blob(st, 1, png.data(), (int)png.size(), SQLITE_TRANSIENT);
        else
            sqlite3_bind_null(st, 1);
        sqlite3_bind_int(st, 2, modelId);
    });
}

int SQLModelRepository::markAllNotIncluded() {
    static const char* SQL =
        "UPDATE models SET is_included = 0 WHERE is_included <> 0;";
    ExecInfo info;
    if (!m_db.exec(SQL, [&](sqlite3_stmt*){}, /*row_cb*/{}, &info)) return 0;
    return info.changes;
}

bool SQLModelRepository::selectAllIncluded(bool select) {
    static const char* SQL = "UPDATE models SET is_selected = ?1 WHERE is_included = 1;";

    return m_db.exec(SQL, [&](sqlite3_stmt* st) { sqlite3_bind_int(st, 1, select ? 1 : 0); });
}

bool SQLModelRepository::addTagToModel(int modelId, std::string_view tag) {
    static const char* UPSERT_TAG = R"(
        INSERT INTO tags(name) VALUES (?1)
        ON CONFLICT(name) DO UPDATE SET name = excluded.name
        RETURNING id;
    )";

    int tagId = -1;
    bool ok = m_db.exec(
        UPSERT_TAG,
        [&](sqlite3_stmt* st){ sqlite3_bind_text(st, 1, std::string(tag).c_str(), -1, SQLITE_TRANSIENT); },
        [&](sqlite3_stmt* st){ tagId = sqlite3_column_int(st, 0); return false; }
    );
    if (!ok || tagId < 0) return false;

    static const char* LINK = "INSERT OR IGNORE INTO model_tags (model_id, tag_id) VALUES (?1, ?2);";
    return m_db.exec(LINK, [&](sqlite3_stmt* st){
        sqlite3_bind_int(st, 1, modelId);
        sqlite3_bind_int(st, 2, tagId);
    });
}

bool SQLModelRepository::removeTagFromModel(int modelId, std::string_view tag) {
    // Resolve tag id in one statement
    static const char* TAG_ID = "SELECT id FROM tags WHERE name = ?1;";
    int tagId = -1;
    m_db.query(TAG_ID,
        [&](sqlite3_stmt* st){ sqlite3_bind_text(st, 1, std::string(tag).c_str(), -1, SQLITE_TRANSIENT); },
        [&](sqlite3_stmt* st){ tagId = sqlite3_column_int(st, 0); return false; });
    if (tagId < 0) return false;

    static const char* DEL = "DELETE FROM model_tags WHERE model_id = ?1 AND tag_id = ?2;";
    return m_db.exec(DEL, [&](sqlite3_stmt* st){
        sqlite3_bind_int(st, 1, modelId);
        sqlite3_bind_int(st, 2, tagId);
    });
}
