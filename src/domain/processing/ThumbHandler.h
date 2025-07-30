#pragma once

#include <QProcess>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

#include "config.h"
#include "IDirectiveHandler.h"
#include "Model.h"

class ThumbHandler : public IDirectiveHandler {
public:
    ThumbHandler(const std::filesystem::path& dataDir, QtJobServiceBase* service, Model& repo)
	: IDirectiveHandler(dataDir, service, repo) {}

    void handle(const JobDescriptor& job, std::atomic<bool>& stopFlag) override {
	emitStart(job);

	// build up paths
	const QString rtExe = QStringLiteral(RT_EXECUTABLE_PATH);
	const QString pngPath = QString::fromStdString((_dataDir / job.fileId / "thumb.png").string());
	// make sure we have parent dirs already created
	QDir().mkpath(QFileInfo(pngPath).absolutePath());

	// check if we've already created this thumb
	if (QFile::exists(pngPath)) {
	    // already have a thumb for this job
	    // TODO: should we verify it's loaded into the Model?
	    emitFinish(job);
	    return;
	}

	// get model from Repo
	ModelData md = _repo.getModelByFilePath(job.sourcePath);

	// verify we have a model and it's still included
	if (md.id <= 0 || md.is_included == false) {
	    emitFinish(job, false);
	    return;
	}
	
	// get our selected object(s) (even though we only care about the first one)
	std::vector<ObjectData> selected = _repo.getSelectedObjectsForModel(md.id);
	if (selected.empty()) {
	    // sanity: this shouldn't be possible if our processor is working correctly
	    emitFinish(job, false);
	    return;
	}
	const std::string& selectedObj = selected.front().name;

	// build the arguments list for rt.exe
	QStringList arguments;
	arguments << "-s512"	    // TODO: auto-scaling sizes?
	//arguments << "-s2048"
	    << "-o" << pngPath
	    << QString::fromStdString(job.sourcePath)
	    << QString::fromStdString(selectedObj);

	// use QProcess so we can cross-platform manage timeout and stopFlag
	QProcess process;
	process.setProcessChannelMode(QProcess::MergedChannels);
	process.setProgram(rtExe);
	process.setArguments(arguments);

	// start the process
	process.start();
	if (!process.waitForStarted()) {
	    emitFinish(job, false);
	    return;
	}

	// poll process for completion / timeout / stopFlag
	bool killed = false;
	QSettings settings;
	int timeLimitMs = settings.value("previewTimer", 30).toInt() * 1000;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeLimitMs);
	while (true) {
	    // finished?
	    if (process.waitForFinished(250))
		break;

	    // stopFlag?
	    if (stopFlag.load()) {
		process.kill();
		process.waitForFinished();
		killed = true;
		break;
	    }

	    // timeout?
	    if (std::chrono::steady_clock::now() >= deadline) {
		process.kill();
		process.waitForFinished();
		killed = true;
		break;
	    }
	}

	// verify we finished successfully
	if (killed || 
	    process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 ||
	    !QFile::exists(pngPath) || QFileInfo(pngPath).size() == 0) {
	    emitFinish(job, false);
	    return;
	}

	// TODO: old code reads thumbnail directly into model - do we still want to do that?
	QFile thumbnailFile(pngPath);
	if (!thumbnailFile.open(QIODevice::ReadOnly)) {
	    emitFinish(job, false);
	    return;
	}
	QByteArray thumbnailData = thumbnailFile.readAll();
	thumbnailFile.close();
	if (thumbnailData.isEmpty()) {
	    emitFinish(job, false);
	    return;
	}
	md.thumbnail.assign(thumbnailData.begin(), thumbnailData.end());
	_repo.updateModel(md.id, md);

	// success
	emitFinish(job);
	// TODO: thumbnailFinished specific signal?
    }

private:
};