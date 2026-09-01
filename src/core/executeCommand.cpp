#include "executeCommand.h"

#include <algorithm>
#include <limits>

#include <QElapsedTimer>
#include <QProcess>
#include <QStringList>

namespace {

int boundedMilliseconds(std::chrono::milliseconds value) {
    return static_cast<int>(std::clamp<long long>(
        value.count(), 1, std::numeric_limits<int>::max()));
}

void stopProcess(QProcess& process) {
    process.kill();
    process.waitForFinished(1000);
}

} // namespace

ProcessResult runProcess(const std::string& program,
                         const std::vector<std::string>& arguments,
                         std::chrono::milliseconds timeout,
                         const std::atomic_bool* cancellation) {
    ProcessResult result;
    if (program.empty()) {
        result.error = "Process executable is empty.";
        return result;
    }
    if (cancellation && cancellation->load(std::memory_order_relaxed)) {
        result.cancelled = true;
        result.error = "Process was cancelled before it started.";
        return result;
    }

    QStringList qtArguments;
    qtArguments.reserve(static_cast<qsizetype>(arguments.size()));
    for (const auto& argument : arguments)
        qtArguments.push_back(QString::fromStdString(argument));

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setProgram(QString::fromStdString(program));
    process.setArguments(qtArguments);

    const int timeoutMs = boundedMilliseconds(timeout);
    QElapsedTimer elapsed;
    elapsed.start();
    process.start();
    if (!process.waitForStarted(std::min(timeoutMs, 5000))) {
        result.error = process.errorString().toStdString();
        if (process.state() != QProcess::NotRunning)
            stopProcess(process);
        return result;
    }
    result.started = true;

    while (process.state() != QProcess::NotRunning) {
        if (cancellation && cancellation->load(std::memory_order_relaxed)) {
            result.cancelled = true;
            result.error = "Process was cancelled.";
            stopProcess(process);
            break;
        }

        const qint64 remaining = static_cast<qint64>(timeoutMs) - elapsed.elapsed();
        if (remaining <= 0) {
            result.timedOut = true;
            result.error = "Process timed out.";
            stopProcess(process);
            break;
        }

        process.waitForFinished(static_cast<int>(std::min<qint64>(remaining, 50)));
    }

    result.output = process.readAll().toStdString();
    result.exitCode = process.exitCode();
    result.crashed = process.exitStatus() == QProcess::CrashExit &&
                     !result.timedOut && !result.cancelled;

    if (!result.success() && result.error.empty()) {
        result.error = result.crashed
            ? "Process crashed."
            : "Process exited with code " + std::to_string(result.exitCode) + ".";
    }
    return result;
}
