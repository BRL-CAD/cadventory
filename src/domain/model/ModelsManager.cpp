#include "ModelsManager.h"
#include "AuditLog.h"

#include <algorithm>

#include "Logger.h"

namespace {

void recordMetadataDiff(const HiddenDir& paths,
                        const ModelData& before,
                        const ModelData& after) {
    AuditLog audit(paths.auditDir());
    struct FieldDiff {
        const char* property;
        const std::string* before;
        const std::string* after;
    };
    const FieldDiff fields[] = {
        {"short_name", &before.short_name, &after.short_name},
        {"primary_file", &before.primary_file, &after.primary_file},
        {"override_info", &before.override_info, &after.override_info},
        {"title", &before.title, &after.title},
        {"author", &before.author, &after.author},
        {"file_path", &before.file_path, &after.file_path},
        {"library_name", &before.library_name, &after.library_name},
        {"long_name", &before.long_name, &after.long_name},
        {"modelers", &before.modelers, &after.modelers},
        {"model_type", &before.model_type, &after.model_type},
        {"aliases", &before.aliases, &after.aliases},
        {"owner_org", &before.owner_org, &after.owner_org},
        {"source_org", &before.source_org, &after.source_org},
        {"suitability", &before.suitability, &after.suitability},
        {"classification", &before.classification, &after.classification},
    };

    for (const auto& field : fields) {
        if (!audit.recordMetadataChange(after, field.property, *field.before, *field.after)) {
            LOG_WARN << "Metadata changed but audit event could not be written for model "
                     << after.id << LOG_ENDL;
        }
    }
}

std::string joinTags(const std::vector<std::string>& tags) {
    std::string joined;
    for (const auto& tag : tags) {
        if (!joined.empty())
            joined += '\n';
        joined += tag;
    }
    return joined;
}

void recordTagDiff(const HiddenDir& paths,
                   const ModelData& model,
                   const std::vector<std::string>& before,
                   const std::vector<std::string>& after) {
    AuditLog audit(paths.auditDir());
    if (!audit.recordMetadataChange(model, "tags", joinTags(before), joinTags(after))) {
        LOG_WARN << "Tags changed but audit event could not be written for model "
                 << model.id << LOG_ENDL;
    }
}

}

ModelsManager::ModelsManager(const std::string& libraryPath) : m_hiddenPaths(libraryPath) {
    m_repo = std::make_unique<SQLModelRepository>(m_hiddenPaths.modelDb());

    // NOTE: we intentionally don't prime the cache; let the caller call refresh() when they want to
    //	     so we keep startup light
}

ModelsManager::~ModelsManager() = default;

void ModelsManager::refresh() {
    std::vector<ModelData> rows = m_repo->getAllModelsHeavy();

    {
	std::lock_guard<std::mutex> lk(m_mutex);
	m_cache = std::move(rows);
    }
    notify();
}

bool ModelsManager::resetDatabase() {
    bool reset = m_repo->reset();
    if (reset) {
	m_cache.clear();
	notify();
    }

    return reset;
}

const std::vector<ModelData>& ModelsManager::getAll_snapshot() const noexcept {
    return m_cache;
}

std::vector<ModelData> ModelsManager::getAll() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_cache; // copy
}

std::vector<ModelData> ModelsManager::getIncludedModels() const {
    std::vector<ModelData> ret;
    {
	std::lock_guard<std::mutex> lk(m_mutex);
	ret.reserve(m_cache.size());
	for (const auto& model : m_cache) {
	    if (model.is_included)
		ret.push_back(model);
	}
    }

    return ret;
}

std::vector<ModelData> ModelsManager::getSelectedModels() const {
    std::vector<ModelData> ret;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        ret.reserve(m_cache.size());
        for (const auto& model : m_cache) {
            if (model.is_selected)
		ret.push_back(model);
        }
    }

    return ret;
}

ModelData ModelsManager::getModelByFilePath(std::string_view filePath) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    for (const auto& model : m_cache) {
	// TODO: normalize with m_hiddenPaths?
	if (model.file_path == filePath)
	    return model;
    }

    // not found
    ModelData notFound;
    notFound.id = -1;
    return notFound;
}

std::vector<unsigned char> ModelsManager::getThumbnail(int modelId) const {
    return m_repo->getThumbnail(modelId);
}

std::vector<std::string> ModelsManager::getTagsForModel(int modelId) const {
    return m_repo->getTagsForModel(modelId);
}

std::optional<ModelData> ModelsManager::insertModel(const ModelData& md) {
    auto inserted = m_repo->insertModel(md);

    if (!inserted)
	return std::nullopt;

    {	// update cache
	std::lock_guard<std::mutex> lk(m_mutex);
	m_cache.push_back(*inserted);
    }

    notify();
    return inserted;
}

bool ModelsManager::updateModel(const ModelData& md) {
    const auto before = m_repo->getModelById(md.id);
    if (!m_repo->updateModel(md.id, md))
	return false;

    if (before) {
        if (const auto after = m_repo->getModelById(md.id))
            recordMetadataDiff(m_hiddenPaths, *before, *after);
    }

    {	// update cache
	std::lock_guard<std::mutex> lk(m_mutex);
	const int idx = indexOfId(md.id);
	if (idx >= 0) {
	    // reload from repo incase name collided or something got normalized
	    if (auto fresh = m_repo->getModelById(md.id))
		m_cache[idx] = *fresh;
	    else
		m_cache[idx] = md;  // fallback
	}
    }

    notify();
    return true;
}

bool ModelsManager::deleteModel(int modelId) {
    if (!m_repo->deleteModel(modelId))
	return false;

    {	// update cache
	std::lock_guard<std::mutex> lk(m_mutex);
	const int idx = indexOfId(modelId);
	if (idx >= 0)
	    m_cache.erase(m_cache.begin() + idx);
    }

    notify();
    return true;
}

bool ModelsManager::setModelIncluded(int modelId, bool included) {
    if (!m_repo->setModelIncluded(modelId, included))
	return false;

    {
	std::lock_guard<std::mutex> lk(m_mutex);
	const int idx = indexOfId(modelId);
	if (idx >= 0)
	    m_cache[idx].is_included = included;
    }

    notify();
    return true;
}

bool ModelsManager::setModelProcessed(int modelId, bool processed) {
    if (!m_repo->setModelProcessed(modelId, processed))
	return false;

    {
	std::lock_guard<std::mutex> lk(m_mutex);
	const int idx = indexOfId(modelId);
	if (idx >= 0)
	    m_cache[idx].is_processed = processed;
    }

    notify();
    return true;
}

bool ModelsManager::setModelSelected(int modelId, bool selected) {
    // TODO: do we want to persist selection in repo?
    if (!m_repo->setModelSelected(modelId, selected))
	return false;

    {
	std::lock_guard<std::mutex> lk(m_mutex);
	const int idx = indexOfId(modelId);
	if (idx >= 0)
	    m_cache[idx].is_selected = selected;
    }

    notify();
    return true;
}

bool ModelsManager::setThumbnail(int modelId, const std::vector<unsigned char>& png) {
    if (!m_repo->setModelThumbnail(modelId, png))
        return false;

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        const int idx = indexOfId(modelId);
        if (idx >= 0) {
            auto& thumbnail = m_cache[static_cast<std::size_t>(idx)].thumbnail;
            thumbnail.assign(reinterpret_cast<const char*>(png.data()),
                             reinterpret_cast<const char*>(png.data() + png.size()));
        }
    }

    notify();
    return true;
}

int ModelsManager::markAllNotIncluded() {
    int affected = m_repo->markAllNotIncluded();

    if (!affected)
	return 0;

    {
	std::lock_guard<std::mutex> lk(m_mutex);
	for (auto& model : m_cache)
	    model.is_included = false;
    }

    notify();
    return affected;
}

bool ModelsManager::selectAllIncluded(bool select) {
    // TODO: should we persist selection status?
    bool ok = m_repo->selectAllIncluded(select);
    if (!ok)
	return false;

    bool anyChanged = false;
    {
	std::lock_guard<std::mutex> lk(m_mutex);
	for (auto& model : m_cache) {
	    if (model.is_included && model.is_selected != select) {
		model.is_selected = select;
		anyChanged = true;
	    }
	}
    }

    if (anyChanged)
	notify();
    return ok;
}

bool ModelsManager::addTagToModel(int modelId, std::string_view tag) {
    const auto before = m_repo->getTagsForModel(modelId);
    if (!m_repo->addTagToModel(modelId, tag))
	return false;

    if (const auto model = m_repo->getModelById(modelId))
        recordTagDiff(m_hiddenPaths, *model, before, m_repo->getTagsForModel(modelId));

    {
	std::lock_guard<std::mutex> lk(m_mutex);
	const int idx = indexOfId(modelId);
	if (idx >= 0) {
	    auto& tags = m_cache[idx].tags;
	    if (std::find(tags.begin(), tags.end(), tag) == tags.end()) {
		tags.emplace_back(tag);
	    }
	}
    }

    notify();
    return true;
}

bool ModelsManager::removeTagFromModel(int modelId, std::string_view tag) {
    const auto before = m_repo->getTagsForModel(modelId);
    if (!m_repo->removeTagFromModel(modelId, tag))
	return false;

    if (const auto model = m_repo->getModelById(modelId))
        recordTagDiff(m_hiddenPaths, *model, before, m_repo->getTagsForModel(modelId));

    {
	std::lock_guard<std::mutex> lk(m_mutex);
	const int idx = indexOfId(modelId);
	if (idx >= 0) {
	    auto& tags = m_cache[idx].tags;
	    tags.erase(std::remove(tags.begin(), tags.end(), tag), tags.end());
	}
    }

    notify();
    return true;
}

void ModelsManager::subscribe(Subscriber cb) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_subscriber = std::move(cb);
}

void ModelsManager::notify() {
    Subscriber cb;
    {	// copy locally so we don't accidentally nest locks
	std::lock_guard<std::mutex> lk(m_mutex);
	cb = m_subscriber;
    }
    if (cb)
	cb();
}

int ModelsManager::indexOfId(int id) const {
    // NOTE: if going to mutate this index; caller MUST hold m_mutex to be thread safe
    for (size_t i = 0; i < m_cache.size(); ++i) {
	if (m_cache[i].id == id)
	    return i;
    }

    // not found
    return -1;
}
