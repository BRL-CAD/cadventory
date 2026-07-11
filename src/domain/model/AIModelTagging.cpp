#include "AIModelTagging.h"

#include <QtConcurrent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QDebug>

AIModelTagging::AIModelTagging(ILLMService& s, const QString &m, QObject *p) : QObject(p), llm(s), modelName(m) {
    connect(&futWatcher, &QFutureWatcher<LLMReply>::finished,
            this, 
            [this] {
                if (cancelled) {
                    futWatcher.cancel();
                    return;
                }
                const auto reply = futWatcher.result();
                if (!reply.success) { 
                    emit taggingFailed(reply.error); 
                    return; 
                }
                if (reply.raw.isEmpty()) { 
                    emit taggingFailed("Empty response"); 
                    return; 
                }

                emit tagsReady(parseTags(reply.raw));
            });
}

void AIModelTagging::cancel() {
    cancelled = true;
}

bool AIModelTagging::taggingEnabled() const {
    return llm.isAvailable();
}

QString AIModelTagging::taggingStatus() const {
    return llm.availabilityError();
}

void AIModelTagging::generateTags(const QString &gFilePath) {
    cancelled = false;

    if (!llm.isAvailable()) {
        emit taggingFailed(llm.availabilityError());
        return;
    }

    LLMRequest req;
    req.prompt   = buildPrompt(gFilePath);
    req.model    = modelName;
    req.timeoutMs = 5 * 60 * 1000;  // 5m

    futWatcher.setFuture(QtConcurrent::run([this, req] {
        return llm.sendPrompt(req);          // returns LLMReply
    }));
}

/* -------- helpers ------------------------------------------------ */
QString AIModelTagging::buildPrompt(const QString &gFile) const
{
    const ModelMetadata md = parser.parseModel(gFile.toStdString());

    QString objs;
    for (const auto &o : md.objectFiles)
        objs += "  - " + QString::fromStdString(o) + '\n';

    return QString(R"(
You are an expert CAD modeller organising CAD files for an engineering database.

Generate exactly 10 relevant tags for categorization and search filtering of this 3D CAD model.

### **File Metadata:**
- Filepath: %1
- Title   : %2
- Objects :
%3

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
)")
        .arg(gFile, QString::fromStdString(md.title), objs);
}

QStringList AIModelTagging::parseTags(const QByteArray &raw) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    const QString resp = doc.isObject() ? doc.object().value("response").toString()
                                        : QString::fromUtf8(raw);

    QStringList lines = resp.split(QRegularExpression("[\r\n]+"),
                                   Qt::SkipEmptyParts);
    // keep first 10 single words
    QStringList tags;
    for (const auto &l : lines) {
        const QString word = l.trimmed().split(' ').first();
        if (!word.isEmpty()) tags << word;
        if (tags.size() == 10) break;
    }
    return tags;
}
