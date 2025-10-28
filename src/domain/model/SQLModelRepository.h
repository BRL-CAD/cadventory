#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>

#include "ModelTypes.h"   // ModelData
#include "SQLiteDB.h"

class SQLModelRepository {
public:
    explicit SQLModelRepository(std::string dbPath);
    ~SQLModelRepository() = default;

    SQLModelRepository(const SQLModelRepository&)            = delete;
    SQLModelRepository& operator=(const SQLModelRepository&) = delete;

    /* --- Lifecycle / schema --- */
    bool reset(); // drop & recreate

    /* --- Reads --- */
					// lightweight 'get all': get JUST the models in the db
    std::vector<ModelData>              getAllModels() const;
					// heavy 'get all': get all models and foreign tables
    std::vector<ModelData>		getAllModelsHeavy(bool with_tags = true,
							  bool with_objects = true) const;
    std::vector<ModelData>              getIncludedModels() const;
    std::vector<ModelData>              getIncludedNotProcessedModels() const;
    std::optional<ModelData>            getModelById(int modelId) const;
    std::optional<ModelData>            getModelByFilePath(std::string filePath) const;
    std::vector<unsigned char>          getThumbnail(int modelId) const;
    std::vector<std::string>            getTagsForModel(int modelId) const;
    bool                                isFileIncluded(std::string filePath) const;

    /* --- Writes --- */
    // insert returns the persisted record (with id and possibly adjusted short_name), or nullopt on failure
    std::optional<ModelData> insertModel(const ModelData& md);
    bool                     updateModel(int id, const ModelData& md);
    bool                     deleteModel(int modelId);

    bool setModelIncluded(int modelId, bool is_included);
    bool setModelProcessed(int modelId, bool is_processed);
    bool setModelSelected(int modelId, bool is_selected);
    bool setModelThumbnail(int modelId, const std::vector<unsigned char>& png);

    int  markAllNotIncluded();                    // rows affected
    bool selectAllIncluded(bool select);          // bulk set selection for included

    bool addTagToModel(int modelId, std::string_view tag);
    bool removeTagFromModel(int modelId, std::string_view tag);

private:
    // helpers
    bool createTables();
    bool shortNameExists(std::string_view short_name) const;
    bool filePathExists(std::string_view file_path) const;
    std::string makeUniqueShortName(std::string base) const;

    // small row mapping helpers
    static ModelData readModelRow(sqlite3_stmt* st);

private:
    SQLiteDB          m_db;
};
