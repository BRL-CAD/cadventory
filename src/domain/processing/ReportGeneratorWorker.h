#ifndef ReportGeneratorWorker_H
#define ReportGeneratorWorker_H

#include <QThread>
#include <QObject>
#include <atomic>
#include "Model.h"
#include <iostream>

class ReportGeneratorWorker : public QObject {
    Q_OBJECT
public:
    explicit ReportGeneratorWorker(Model* model = nullptr, std::string output_directory = "", std::string label = "", QObject* parent = nullptr);
public slots:
    void process();

signals:
    void processingGistCall(const QString& file);
    void successfulGistCall(const QString& path_gist_output);
    void failedGistCall(const QString& filepath, const QString& errorMessage, const QString& command);
    void finishedReport();
    void finished();

private:
    std::string label;
    std::string output_directory;
    Model* model;
};

#endif // ReportGeneratorWorker_H
