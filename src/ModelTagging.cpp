#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <fstream>
#include <regex>
#include "ModelTagging.h"
#include <QProcess>
#include <QDebug>
#include <QtConcurrent>
#include <QFuture>
#include "executeCommand.h"

void logToFile(const std::string& message) {
    static std::mutex logMutex; 
    std::lock_guard<std::mutex> lock(logMutex);

    std::ofstream logFile("ai_tagging_log.txt", std::ios_base::app); 
    if (logFile.is_open()) {
        std::time_t now = std::time(nullptr);
        char* dt = std::ctime(&now);
        dt[strlen(dt) - 1] = '\0';
        logFile << "[" << dt << "] " << message << "\n";
    }
}

bool ModelTagging::checkOllamaAvailability() {
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

    // Not found anywhere
    return false;
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

    return false;
#endif
}
 
bool ModelTagging::checkModelAvailability(const std::string& modelName) {
#ifdef _WIN32
    // Windows implementation using QProcess
    QString command = m_ollamaPath;
    QStringList args;
    args << "list";

    QProcess process;
    process.start(command, args);
    process.waitForFinished();
    QString output = process.readAllStandardOutput();

    logToFile("checkModelAvailability: raw output = " + output.toStdString());
    bool found = output.contains(QString::fromStdString(modelName));
    logToFile("checkModelAvailability: model \"" + modelName + "\" found = " + std::to_string(found));
    return found;
#else
    // Linux/Mac implementation - use QProcess for consistency
    QProcess process;
    process.setProgram(m_ollamaPath);
    process.setArguments(QStringList() << "list");

    process.start();
    process.waitForFinished();
    QString output = process.readAllStandardOutput();

    logToFile("checkModelAvailability (Linux/mac): raw output = " + output.toStdString());
    bool found = output.contains(QString::fromStdString(modelName));
    logToFile("checkModelAvailability (Linux/mac): model \"" + modelName + "\" found = " + std::to_string(found));
    return found;
#endif
}

ModelTagging::ModelTagging(){
    // instantiate the parser
    parser = ModelParser();
}

ModelTagging::~ModelTagging(){
    // destructor implementation
    if (tagProcess) {
        tagProcess->kill();
        tagProcess->deleteLater();
    }
}

void ModelTagging::generateTags(const std::string& filepath) {
	m_generationCanceled = false; // Reset the cancellation 
    m_accumulatedOutput.clear();

    logToFile("generateTags() called with: " + filepath);

    auto blockingTask = [this, filepath]() -> bool {
        // Check if Ollama is available.
        if (!checkOllamaAvailability()) {
            std::cerr << "ERROR: Ollama is not available." << std::endl;
            logToFile("ERROR: Ollama is not available.");
            return false;
        }
        // Check if the model is available.
        if (!checkModelAvailability(this->modelName)) {
            std::cerr << "ERROR: Model " << this->modelName << " is not available." << std::endl;
			logToFile("ERROR: Model " + this->modelName + " is not available.");
            return false;
        }

        // Parse metadata.
        logToFile("Ollama and model available. Parsing metadata...");
        ModelMetadata metadata = parser.parseModel(filepath);
        logToFile("Metadata parsed. Title: " + metadata.title + ", Object count: " + std::to_string(metadata.objectFiles.size()));

        // Build the prompt.
        std::ostringstream objectPathsStream;
        for (const auto& object : metadata.objectFiles) {
            objectPathsStream << "  - " << object << "\n";
        }
        std::ostringstream prompt;
        prompt << R"(
            You are an expert CAD modeler organizing CAD files for an engineering database.
            
            Generate exactly 10 relevant tags for categorization and search filtering of this 3D CAD model.
            
            ### **File Metadata:**
            - **Filepath:** )" << filepath << R"(
            - **Title:** )" << metadata.title << R"(
            - **Object Paths:**  
        )" << objectPathsStream.str() << R"(

            ### **Instructions:**
            - **ONLY use words directly related to the object in the CAD model.**  
            - **Do NOT include metadata like names, file paths, BRL-CAD, or dates.**  
            - **Prioritize useful search terms for categorization (e.g., mechanical, architectural, vehicle, tool).**  
            - **Infer object identity from file names, object names, and folder structure.**  
            - **Do NOT make them generic (e.g., object, model, 3D).**
            - **Do NOT assume additional details beyond what's in the metadata.**
            - **Your output must be EXACTLY 10 single-word tags, one per line with no numbering or additional formatting.**

            ### Example:
            For a teapot model, appropriate tags would be:
            Teapot
            Container
            Lid
            Ceramic
            Tableware
            Kitchenware
            Beverage
            Drinking
            Serving
            Decorative
        )";

        m_promptText = prompt.str();
        logToFile("Prompt created, length: " + std::to_string(m_promptText.length()));

        return true;
        };

    QFuture<bool> future = QtConcurrent::run(blockingTask);
    QFutureWatcher<bool>* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher]() {
        bool success = watcher->result();
        watcher->deleteLater();

        if (m_generationCanceled) {
            logToFile("Generation canceled during blocking tasks.");
            qDebug() << "Generation was canceled during blocking tasks.";
            emit tagGenerationCanceled();
            return;
        }

        if (!success) {
            logToFile("Blocking task failed. Emitting empty tags.");
            emit tagsGenerated({});
            return;
        }

        logToFile("Starting Ollama QProcess...");
        if (tagProcess) {
            tagProcess->deleteLater();
        }
        tagProcess = new QProcess(this);
        connect(tagProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ModelTagging::onTagProcessFinished);

        connect(tagProcess, &QProcess::readyReadStandardOutput, this, &ModelTagging::handleProcessOutput);

        tagProcess->setProgram(m_ollamaPath);
        tagProcess->setArguments(QStringList() << "run" << "llama3");

        tagProcess->start();

        logToFile("Writing prompt to Ollama...");
        QByteArray promptData = QByteArray::fromStdString(m_promptText);
        tagProcess->write(promptData);
        tagProcess->closeWriteChannel();
        logToFile("QProcess started with: ollama run llama3");
     });

    watcher->setFuture(future);
}

void ModelTagging::cancelTagGeneration() {
	m_generationCanceled = true;
    if (tagProcess && tagProcess->state() == QProcess::Running) {
        tagProcess->terminate();
        if (!tagProcess->waitForFinished(2000)) {
            tagProcess->kill();
        }
        qDebug() << "Tag generation canceled (process killed).";
        emit tagGenerationCanceled();
    }
    else {
        qDebug() << "Tag generation canceled before process started.";
        emit tagGenerationCanceled();
    }
}

void ModelTagging::handleProcessOutput() {
    QByteArray output = tagProcess->readAllStandardOutput();
    m_accumulatedOutput += QString::fromUtf8(output);
}

void ModelTagging::onTagProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    qDebug() << "[ModelTagging] onTagProcessFinished: exitCode=" << exitCode
        << "exitStatus=" << exitStatus;
    logToFile("onTagProcessFinished: exitCode = " + std::to_string(exitCode) +
        ", exitStatus = " + std::to_string(exitStatus));
    Q_UNUSED(exitCode);
    Q_UNUSED(exitStatus);

    std::vector<std::string> tags;

    std::string content = m_accumulatedOutput.toStdString();

	// use regex to extract tags
    std::regex tagPattern(R"((?:^|\n)([A-Za-z]+)(?:\n|$))");
    std::sregex_iterator it(content.begin(), content.end(), tagPattern);
    std::sregex_iterator end;
    int count = 0;
    while (it != end && count < 10) {
        std::string tag = (*it)[1];
        if (!tag.empty()) {
            tags.push_back(tag);
            count++;
        }
        ++it;
    }

	// if regex didn't find enough tags, use fallback parsing which is less strict
    if (tags.size() < 10) {
        logToFile("Tag count less than 10. Fallback parsing used.");
        std::istringstream contentStream(content);
        std::string line;
        tags.clear();
        while (std::getline(contentStream, line) && tags.size() < 10) {
            line.erase(0, line.find_first_not_of(" \t\n\r\f\v"));
            line.erase(line.find_last_not_of(" \t\n\r\f\v") + 1);
            if (line.empty() || line.find(' ') != std::string::npos || !std::isalpha(line[0])) {
                continue;
            }
            tags.push_back(line);
        }
    }

    // Emit the signal with the generated tags.
    emit tagsGenerated(tags);

    qDebug() << "[ModelTagging] tagsGenerated signal emitted with "
        << tags.size() << "tags";
    logToFile("Final tag count: " + std::to_string(tags.size()));

    tagProcess->deleteLater();
    tagProcess = nullptr;

    m_accumulatedOutput.clear();
}
