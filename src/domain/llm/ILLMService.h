#pragma once

#include <QString>
#include <QByteArray>
#include <optional>

struct LLMRequest {
    QString     prompt;			    // user prompt
    QString     model;			    // "llama3", "gpt-4o", etc.
    int         timeoutMs = 5 * 60 * 1000;  // 5m default
};

struct LLMReply {
    QByteArray  raw;			    // response
    bool        success = true;
    QString     error;			    // empty if success == true
};

/* Pure abstract base */
class ILLMService
{
public:
    virtual ~ILLMService() = default;

    // return's true if the model is available and usable
    virtual bool isAvailable() const = 0;

    // 'brains'
    virtual LLMReply sendPrompt(const LLMRequest &req) = 0;
};
