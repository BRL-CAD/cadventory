#include "OllamaCLIService.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>

/*** virtual class implementations ***/
OllamaCliService::OllamaCliService(QString exe_path) {
    if (ensureExe(exe_path)) {
        // go ahead and start if we can
        ensureDaemon();
    }
}

bool OllamaCliService::isAvailable() const {
    // check we have a running daemon
    return ensureDaemon();
}

LLMReply OllamaCliService::sendPrompt(const LLMRequest &r) {
    LLMReply res;

    if (!isAvailable()) {
        res.success = false;
        res.error   = "ollama not available";
        return res;
    }
    if (!ensureModel(r.model)) {
        res.success = false;
        res.error   = "could not use requested model";
        return res;
    }

    /* assemble CLI args */
    QStringList args { "run", r.model, r.prompt };

    int code = -1;
    QByteArray raw = runCmd(args, r.timeoutMs, &code);

    if (code != 0 || raw.isEmpty()) {
        res.success = false;
        res.error   = "ollama run failed (exit " + QString::number(code) + ")";
        return res;
    }

    res.raw = raw;
    return res;
}

OllamaCliService::~OllamaCliService() {
    // shutdown daemon
    if (m_daemon) {
        m_daemon->terminate();
        if (!m_daemon->waitForFinished(3000))
            m_daemon->kill();
    }
}

bool OllamaCliService::ensureExe(QString exe_path) {
    if (!exe_path.isEmpty() && QFile::exists(exe_path)) {
        m_exe = exe_path;
        return true;
    }

#if 0   // TODO/FIXME: see if we can find a system installed ollama
#ifdef _WIN32
    // First try to find ollama in our application directory
    QString appDir = QCoreApplication::applicationDirPath();
    QString ollamaLocalPath = appDir + "/ollama/ollama.exe";

    QFileInfo fileInfo(ollamaLocalPath);
    if (fileInfo.exists()) {
        logToFile("Found bundled Ollama at: " + ollamaLocalPath.toStdString());
        m_ollamaPath = ollamaLocalPath;
        return true;
    }

    // Windows command using our blocking function as fallback
    std::string output = executeCommandNoWindow("cmd /C where ollama");
    logToFile("checkOllamaAvailability PATH check: output = " + output);

    // If ollama is found in PATH
    if (output.find("ollama") != std::string::npos) {
        logToFile("Found Ollama in PATH");
        m_ollamaPath = "ollama";
        return true;
    }
#else
    // First check local path for Linux/Mac
    QString appDir = QCoreApplication::applicationDirPath();
    QString ollamaLocalPath = appDir + "/ollama/ollama";

    QFileInfo fileInfo(ollamaLocalPath);
    if (fileInfo.exists() && fileInfo.isExecutable()) {
        logToFile("Found bundled Ollama at: " + ollamaLocalPath.toStdString());
        m_ollamaPath = ollamaLocalPath;
        return true;
    }

    // Check PATH as fallback
    int result = std::system("which ollama >/dev/null 2>&1");
    logToFile("checkOllamaAvailability PATH check: result = " + std::to_string(result));

    if (result == 0) {
        m_ollamaPath = "ollama";
        return true;
    }
#endif
#endif

    // not found anywhere
    return false;
}

bool OllamaCliService::ensureDaemon() const {
    // if we previously started it, make sure its still alive
    if (m_daemon && m_daemon->state() == QProcess::Running)
        return true;

    if (!m_daemon) {
        // spawn new daemon
        m_daemon = new QProcess(nullptr);
        m_daemon->setProgram(m_exe);
        m_daemon->setArguments({"serve"});
        m_daemon->setProcessChannelMode(QProcess::MergedChannels);
        m_daemon->start();
    } else {
        // try to restart
        m_daemon->start();
    }

    if (!m_daemon->waitForStarted(3000))
        return false;

    return m_daemon->state() == QProcess::Running;
}

bool OllamaCliService::ensureModel(const QString &model) const {
    // we've already verified we have this model?
    if (model == m_verified_model)
        return true;

    // check for model in list
    auto list = runCmd({"list", model}, 10000);
    // expect output in format
    // HEADER_ROW \n
    // MODEL_NAME \n
    // so, if we have more than one newline (ie more than just a header row) assume we have the model listed
    if (list.count('\n') > 1) {
        m_verified_model = model;
        return true;
    }

    // attempt to pull if missing
    int code = -1;
    runCmd({"pull", model}, 10 * 60 * 1000, &code);   // up to 10 min
    bool pull_successful = (code == 0);
    if (pull_successful)
        m_verified_model = model;

    return pull_successful;
}

QByteArray OllamaCliService::runCmd(const QStringList &args, int timeoutMs, int *exitCode) const {
    // use QProcess for cross-platform running
    QProcess p;
    p.setProgram(m_exe);                                    // ollama executable
    p.setArguments(args);
    p.setProcessChannelMode(QProcess::SeparateChannels);    // avoid spinner writing in stderr
    p.start();

    if (!p.waitForStarted())
        return {};

    p.closeWriteChannel();
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished();
    }

    if (exitCode)
        *exitCode = (p.exitStatus() == QProcess::NormalExit) ? p.exitCode() : -1;

    return p.readAllStandardOutput();
}
