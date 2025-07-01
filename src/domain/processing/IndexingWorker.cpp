#include "IndexingWorker.h"
#include "ProcessGFiles.h"
#include "Model.h"
#include <QDebug>
#include <filesystem>

namespace fs = std::filesystem;

IndexingWorker::IndexingWorker(Library* library, QObject* parent)
    : QObject(parent),
    library(library),
    m_stopRequested(false),
    m_reindexRequested(false) {}


void IndexingWorker::stop() {
    qDebug() << "IndexingWorker::stop() called";
    m_stopRequested.store(true);
}

void IndexingWorker::requestReindex() {
    qDebug() << "Indexing reindex requested";
    m_reindexRequested.store(true);
}

void IndexingWorker::process() {
    qDebug() << "IndexingWorker::process() started";

    while (true) {
        // Reset reindex request for this iteration
        m_reindexRequested.store(false);

        ProcessGFiles processor(library->model);

        // Retrieve models that need processing
        std::vector<ModelData> modelsToProcess = library->model->getIncludedNotProcessedModels();
        int totalFiles = modelsToProcess.size();
        int processedFiles = 0;

        if (totalFiles == 0) {
            emit progressUpdated("No files to process", 100);
            // If no models to process, check if reindex was requested
            if (!m_reindexRequested.load()) {
                emit finished();
                qDebug() << "IndexingWorker::process() finished with no models to process";
                return;
            }
            // If reindex was requested, continue to the next iteration
            continue;
        }

        for (const auto& modelData : modelsToProcess) {
            if (m_stopRequested.load()) {
                qDebug() << "IndexingWorker::process() stopping due to stop request";
                break;
            }
            int percentage = (processedFiles * 100) / totalFiles;

            // Emit progress signal before processing the file
            QString currentObject = QString::fromStdString(modelData.short_name);
            emit progressUpdated(currentObject, percentage);
            emit modelProcessed(modelData.id);

            processor.processGFile(modelData);
            processedFiles++;
        }

        // Emit final progress signal to indicate completion
        emit progressUpdated("Processing complete", 100);

        // Check if a reindex was requested during processing
        if (!m_reindexRequested.load()) {
            emit finished();
            qDebug() << "IndexingWorker::process() finished";
            return;
        }
    }
}
