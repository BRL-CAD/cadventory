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

    HandlerResult handle(const JobDescriptor& job, std::atomic<bool>& stopFlag) override {
	// check this job is still needed
        auto ctx = needsHandled(job, "png", ExistingBehavior::Load);
        if (!ctx)
	    return {true,"no work to do"};

	// already have the png generated; just update in model repo
	if (ctx->alreadyExists) {
	    bool success = _repo.updateThumbnailFromFile(ctx->modeldata->id, ctx->outputPath.string());
	    std::string success_str = success ? "updated thumbnail from png" : "failed to update thumbnail";
	    return {success, success_str};
	}

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
	    return {false, "process.waitForStarted() failed"};
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
	    return {false, "process didn't finish successfully"};
	}

	bool success = _repo.updateThumbnailFromFile(ctx->modeldata->id, ctx->outputPath.string());
	std::string success_str = success ? "updated thumbnail from png" : "failed to update thumbnail";
	return {success, success_str};
	// TODO: thumbnailFinished specific signal?
    }

private:
};