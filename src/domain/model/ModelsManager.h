#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "ModelTypes.h"          // ModelData, etc.
#include "HiddenDir.h"
#include "SQLModelRepository.h"

class ModelsManager {
public:
    explicit ModelsManager(const std::string& dbPath);
    virtual ~ModelsManager();

    // non-copyable, movable (owns unique_ptr)
    ModelsManager(const ModelsManager&)            = delete;
    ModelsManager& operator=(const ModelsManager&) = delete;
    ModelsManager(ModelsManager&&)                 = default;
    ModelsManager& operator=(ModelsManager&&)      = default;

    /* --- State Management --- */
    // load repo data into memory
    void refresh();
    // reset
    bool resetDatabase();

    /* --- Reads (served from in-memory) --- */
    const std::vector<ModelData>&     getAll_snapshot() const noexcept;	// fast, not mutatable
    std::vector<ModelData>            getAll() const;			// returns a copy
    std::vector<ModelData>            getIncludedModels() const;
    std::vector<ModelData>            getSelectedModels() const;
    // if not found, returns modelData with id == -1
    ModelData                         getModelByFilePath(std::string_view filePath) const;

    /* --- Heavy Reads (directly from repo) --- */
    std::vector<unsigned char>        getThumbnail(int modelId) const;
    std::vector<std::string>          getTagsForModel(int modelId) const;

    /* --- Mutations (syncs with repo, then notifies subscriber) --- */
    ModelData                         insertModel(const ModelData& md);
    bool                              updateModel(const ModelData& md);
    bool                              deleteModel(int modelId);

    bool                              setModelIncluded(int modelId, bool v);
    bool                              setModelProcessed(int modelId, bool v);
    bool			      setModelSelected(int modelId, bool value);
    bool                              setThumbnail(int modelId, const std::vector<unsigned char>& png);

    int                               markAllNotIncluded();         // returns affected rows
    bool                              selectAllIncluded(bool select);

    bool                              addTagToModel(int modelId, std::string_view tag);
    bool                              removeTagFromModel(int modelId, std::string_view tag);

    /* --- Change Notification (qt service should subscribe) --- */
    using Subscriber = std::function<void()>;
    void subscribe(Subscriber cb);

protected:
    // owned impl
    std::unique_ptr<SQLModelRepository> m_repo;

    // in-memory state
    std::vector<ModelData>              m_cache;

    // subscriber to notify() of changes
    mutable std::mutex                  m_mutex;
    Subscriber                          m_subscriber;

    // helpers
    void notify();		    // call after any cache / repo changing mutation
    int  indexOfId(int id) const;

private:
    HiddenDir m_hiddenPaths;
};