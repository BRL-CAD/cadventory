#pragma once

#include <QObject>
#include <QFutureWatcher>
#include <QString>
#include <vector>

#include "ILLMService.h"
#include "ModelParser.h"


class AIModelTagging : public QObject
{
    Q_OBJECT
public:
    explicit AIModelTagging(ILLMService& svc,
                            const QString &model = "llama3",
                            QObject *parent = nullptr);

    bool taggingEnabled() const;
    QString taggingStatus() const;
    void generateTags(const QString &gFilePath);
    void cancel();

signals:
    void tagsReady(const QStringList &tags);
    void taggingFailed(const QString &reason);

private:
    QString buildPrompt(const QString &gFile) const;
    QStringList parseTags(const QByteArray &raw) const;

    ILLMService                      &llm;       // not owned
    QString                          modelName;
    ModelParser                      parser;
    QFutureWatcher<LLMReply>         futWatcher;
    bool                             cancelled = false;
};
