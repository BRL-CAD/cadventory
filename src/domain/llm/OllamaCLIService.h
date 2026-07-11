#pragma once

#include "ILLMService.h"

#include <QString>

/* use local ollama CLI */
class OllamaCliService : public ILLMService
{
public:
    explicit OllamaCliService(QString exe_path = "");
    ~OllamaCliService();

    /* ILLMService overrides */
    bool       isAvailable() const override;
    QString    availabilityError() const override;
    LLMReply   sendPrompt(const LLMRequest &req)   override;

private:
    /* helpers */
    // ensures we have a valid path to an executable
    bool       ensureExe(QString exe_path = "");
    // ensures we have a the desired model
    bool       ensureModel(const QString &model) const;
    QByteArray runCmd(const QStringList &args,
                      int timeoutMs,
                      int *exitCode = nullptr,
                      QString *standardError = nullptr) const;

    QString m_exe;                          // path to ollama executable
    mutable QString m_verified_model;       // last verified model from 'ensureModel()'
    mutable QString m_availabilityError;
};
