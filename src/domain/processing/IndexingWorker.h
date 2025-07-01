#ifndef INDEXINGWORKER_H
#define INDEXINGWORKER_H

#include <QObject>
#include <atomic>
#include "Library.h"

class IndexingWorker : public QObject {
    Q_OBJECT
public:
    explicit IndexingWorker(Library* library, QObject* parent = nullptr);
    void stop();

public slots:
    void process();
    void requestReindex();

signals:
    void modelProcessed(int modelId);
    void progressUpdated(const QString& currentObject, int percentage);
    void finished();

private:
    Library* library;
    std::atomic<bool> m_stopRequested;
    std::atomic<bool> m_reindexRequested;
    bool previewFlag;
};

#endif // INDEXINGWORKER_H
