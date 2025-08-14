#include "IDirectiveHandler.h"

void IDirectiveHandler::emitStart(const JobDescriptor& job) const {
    if (!_service)
        return;

    _service->directiveStarted(QString::fromStdString(job.directive), QString::fromStdString(job.fileId));
}

void IDirectiveHandler::emitFinish(const JobDescriptor& job, bool ok) const {
    if (!_service)
        return;

    _service->directiveFinished(QString::fromStdString(job.directive), QString::fromStdString(job.fileId), ok);
}

std::optional<HandlerContext> IDirectiveHandler::needsHandled(const JobDescriptor& job, std::string ext) {
    // fetch model data for this job's source file
    ModelData existing = _repo.getModelByFilePath(job.sourcePath);

    // verify we still have the model in db and it's still selected
    if (!existing.id || !existing.is_included)
        return std::nullopt;

    // smart-ptr wrapper
    auto mdl = std::make_shared<ModelData>(std::move(existing));

    // special case: if this is the initial process job, that's all we need
    if (job.directive == "process")
        return HandlerContext{ {}, "", mdl };

    // build our output path
    using std::filesystem::path;
    const path dir = _dataDir / job.fileId;
    std::string file_stem = job.directive;
    if (!ext.empty())
        file_stem += "." + ext;

    const path outPath = dir / file_stem;

    // verify we don't already have this output file
    if (exists(outPath))
        return std::nullopt;

    // lookup our primary object
    std::vector<ObjectData> selected = _repo.getSelectedObjectsForModel(mdl->id);
    std::string primaryObj = !selected.empty() ? selected.front().name : "";

    // NOTE: we're expecting the job.fileId to have the pattern ab/abc123/objName/
    path fidPath = path(job.fileId).lexically_normal();
    std::string jobPrimaryObj = fidPath.filename().string();
    if (jobPrimaryObj.empty())          // path ended with '/'
        jobPrimaryObj = fidPath.parent_path().filename().string();

    // verify we have a primary object and it matches request
    if (primaryObj.empty() || primaryObj != jobPrimaryObj)
        return std::nullopt;

    // sanity: make sure parent directories are created
    create_directories(dir);

    // got everything we need - let the handler do the job
    return HandlerContext{ outPath, primaryObj, mdl };
}
