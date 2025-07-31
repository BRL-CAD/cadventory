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
	// check this job is still needed
        auto ctx = needsHandled(job, "png");
        if (!ctx)
	    // TODO: if we failed because we already have a png, should we verify it's loaded in the model
            return;

	emitStart(job);

	// get our rt executable path
	const QString rtExe = QStringLiteral(RT_EXECUTABLE_PATH);

	// build the arguments list for rt.exe
	QStringList arguments;
	arguments << "-s512"	    // TODO: auto-scaling sizes?
	//arguments << "-s2048"
	    << "-o" << QString::fromStdString(ctx->outputPath.string())
	    << QString::fromStdString(job.sourcePath)
	    << QString::fromStdString(ctx->primaryObject);

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
	    !QFile::exists(ctx->outputPath) || QFileInfo(ctx->outputPath).size() == 0) {
	    emitFinish(job, false);
	    return;
	}

	// TODO: old code reads thumbnail directly into model - do we still want to do that?
	QFile thumbnailFile(ctx->outputPath);
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
	ModelData* ctx_md = ctx->modeldata.get();
	ctx_md->thumbnail.assign(thumbnailData.begin(), thumbnailData.end());
	_repo.updateModel(ctx_md->id, *ctx_md);

	// success
	emitFinish(job);
	// TODO: thumbnailFinished specific signal?
    }

private:
};