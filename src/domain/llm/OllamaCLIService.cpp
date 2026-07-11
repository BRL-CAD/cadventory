#include "OllamaCLIService.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace {

QString text(const char* source) {
    return QCoreApplication::translate("OllamaCliService", source);
}

}

/*** virtual class implementations ***/
OllamaCliService::OllamaCliService(QString exe_path) {
    ensureExe(exe_path);
}

bool OllamaCliService::isAvailable() const {
    if (m_exe.isEmpty()) {
        if (m_availabilityError.isEmpty())
            m_availabilityError = text("Ollama executable was not found. Configure its path in Settings or add it to PATH.");
        return false;
    }

    int exitCode = -1;
    QString standardError;
    runCmd({"list"}, 5000, &exitCode, &standardError);
    if (exitCode != 0) {
        m_availabilityError = standardError.trimmed();
        if (m_availabilityError.isEmpty())
            m_availabilityError = text("Ollama is installed but its local service is unavailable.");
        return false;
    }

    m_availabilityError.clear();
    return true;
}

QString OllamaCliService::availabilityError() const {
    return m_availabilityError;
}

LLMReply OllamaCliService::sendPrompt(const LLMRequest &r) {
    LLMReply res;

    if (!isAvailable()) {
        res.success = false;
        res.error = availabilityError();
        return res;
    }
    if (!ensureModel(r.model)) {
        res.success = false;
        res.error = text("Ollama model '%1' is not installed. Select an installed model in Settings or install it with 'ollama pull %1'.")
                        .arg(r.model);
        return res;
    }

    /* assemble CLI args */
    QStringList args { "run", r.model, r.prompt };

    int code = -1;
    QString standardError;
    QByteArray raw = runCmd(args, r.timeoutMs, &code, &standardError);

    if (code != 0 || raw.isEmpty()) {
        res.success = false;
        res.error = standardError.trimmed();
        if (res.error.isEmpty())
            res.error = text("Ollama did not return tags (exit %1).").arg(code);
        return res;
    }

    res.raw = raw;
    return res;
}

OllamaCliService::~OllamaCliService() = default;

bool OllamaCliService::ensureExe(QString exe_path) {
    const QString candidate = exe_path.isEmpty()
        ? QStandardPaths::findExecutable("ollama")
        : QFileInfo(exe_path).absoluteFilePath();
    const QFileInfo file(candidate);
    if (!file.exists() || !file.isFile() || !file.isExecutable()) {
        m_exe.clear();
        m_availabilityError = exe_path.isEmpty()
            ? text("Ollama executable was not found on PATH.")
            : text("Configured Ollama executable is not usable: %1").arg(exe_path);
        return false;
    }

    m_exe = file.absoluteFilePath();
    m_availabilityError.clear();
    return true;
}

bool OllamaCliService::ensureModel(const QString &model) const {
    // we've already verified we have this model?
    if (model == m_verified_model)
        return true;

    int exitCode = -1;
    runCmd({"show", model}, 10000, &exitCode);
    if (exitCode == 0) {
        m_verified_model = model;
        return true;
    }
    return false;
}

QByteArray OllamaCliService::runCmd(const QStringList &args, int timeoutMs, int *exitCode,
                                    QString *standardError) const {
    // use QProcess for cross-platform running
    QProcess p;
    p.setProgram(m_exe);                                    // ollama executable
    p.setArguments(args);
    p.setProcessChannelMode(QProcess::SeparateChannels);    // avoid spinner writing in stderr
    p.start();

    if (!p.waitForStarted()) {
        if (standardError)
            *standardError = text("Could not start Ollama executable: %1").arg(m_exe);
        return {};
    }

    p.closeWriteChannel();
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished();
        if (standardError)
            *standardError = text("Ollama command timed out.");
    }

    if (exitCode)
        *exitCode = (p.exitStatus() == QProcess::NormalExit) ? p.exitCode() : -1;

    if (standardError && standardError->isEmpty())
        *standardError = QString::fromUtf8(p.readAllStandardError());

    return p.readAllStandardOutput();
}
