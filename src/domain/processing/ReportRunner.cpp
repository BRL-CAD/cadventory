#include "ReportRunner.h"

#include "Logger.h"
#include "Model.h"
#include "HiddenDir.h"
#include "SQLJobQueue.h"
#include "FilesystemIndexer.h"
#include "ProcessGFiles.h"
#include "GistHandler.h"
#include "OllamaCLIService.h"
#include "AIModelTagging.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QString>
#include <QStringList>

#include <atomic>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

// Join a model's tags into a single display string.
QString joinTags(const std::vector<std::string>& tags) {
    QStringList out;
    for (const auto& t : tags)
        out << QString::fromStdString(t);
    return out.join(", ");
}

// Best-effort AI tagging of every processed model.  No-op (with a log) when no
// local LLM is available, so a missing Ollama install never blocks the report.
void tagModels(Model& repo, const std::vector<ModelData>& models) {
    QSettings settings;
    OllamaCliService llm(settings.value("ai/ollamaExecutable").toString());
    if (!llm.isAvailable()) {
        LOG_WARN << "[ReportRunner] AI tagging skipped: " << llm.availabilityError() << LOG_ENDL;
        return;
    }

    const QString modelName = settings.value("ai/ollamaModel", "llama3").toString();
    LOG_INFO << "[ReportRunner] AI tagging with model '" << modelName << "'" << LOG_ENDL;
    AIModelTagging tagger(llm, modelName);

    // one event loop reused across models; tagging itself runs on a worker thread
    QEventLoop loop;
    QStringList lastTags;
    QObject::connect(&tagger, &AIModelTagging::tagsReady, &loop,
                     [&](const QStringList& t) { lastTags = t; loop.quit(); });
    QObject::connect(&tagger, &AIModelTagging::taggingFailed, &loop,
                     [&](const QString& err) { lastTags.clear();
                         LOG_WARN << "[ReportRunner] tagging failed: " << err << LOG_ENDL;
                         loop.quit(); });

    const HiddenDir& paths = repo.getHiddenPaths();
    for (const auto& md : models) {
        const std::string abs = paths.resolveRelToLib(md.file_path);
        lastTags.clear();
        tagger.generateTags(QString::fromStdString(abs));
        loop.exec();

        if (lastTags.isEmpty())
            continue;
        repo.removeAllTagsFromModel(md.id);
        for (const auto& t : lastTags)
            repo.addTagToModel(md.id, t.toStdString());
        LOG_INFO << "[ReportRunner] tagged " << md.short_name << " with "
                 << lastTags.size() << " tags" << LOG_ENDL;
    }
}

} // namespace

int ReportRunner::run(const Options& opt) {
    // resolve and validate the library root
    std::error_code ec;
    const fs::path libAbs = fs::absolute(opt.libraryPath, ec).lexically_normal();
    if (ec || !fs::is_directory(libAbs)) {
        LOG_ERR << "[ReportRunner] library path is not a directory: " << opt.libraryPath << LOG_ENDL;
        return 1;
    }
    const std::string lib = libAbs.generic_string();
    LOG_INFO << "[ReportRunner] library: " << lib << LOG_ENDL;

    // Resolve the final output before setting up the report cache.
    std::string outPdf = opt.outputPdf;
    if (outPdf.empty()) {
        const QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        outPdf = (libAbs / CADV_DOTFOLDER / "reports" /
                  ("report_" + ts.toStdString() + ".pdf")).generic_string();
    } else {
        outPdf = fs::absolute(outPdf, ec).lexically_normal().generic_string();
    }
    fs::create_directories(fs::path(outPdf).parent_path(), ec);
    if (ec) {
        LOG_ERR << "[ReportRunner] could not create output directory: " << ec.message() << LOG_ENDL;
        return 1;
    }

    // Keep metadata and gist scratch/cache outside the source library.  The
    // stable cache supports --no-render and lets interrupted reports resume,
    // while resetting report metadata cannot alter a library database.
    const QByteArray cacheKey = QCryptographicHash::hash(
        QByteArray::fromStdString(lib), QCryptographicHash::Sha256).toHex().left(20);
    const fs::path workspace = fs::temp_directory_path() /
        "cadventory-report-cache" / cacheKey.constData();
    fs::create_directories(workspace, ec);
    if (ec) {
        LOG_ERR << "[ReportRunner] could not create report cache: " << ec.message() << LOG_ENDL;
        return 1;
    }
    LOG_INFO << "[ReportRunner] cache: " << workspace.generic_string() << LOG_ENDL;

    Model repo(workspace.generic_string());
    repo.resetDatabase();
    const HiddenDir& paths = repo.getHiddenPaths();

    // 1) index: scan for .g files, deepening if the default depth finds none
    int depth = std::max(opt.depth, 1);
    std::vector<std::string> gfiles;
    for (;;) {
        FilesystemIndexer indexer(lib.c_str(), depth);
        gfiles = indexer.findFilesWithSuffixes({".g"});
        if (!gfiles.empty() || depth >= 64)
            break;
        depth *= 2;
        LOG_INFO << "[ReportRunner] no .g files found; deepening scan to depth " << depth << LOG_ENDL;
    }
    LOG_INFO << "[ReportRunner] found " << gfiles.size() << " .g file(s) at depth " << depth << LOG_ENDL;
    if (gfiles.empty()) {
        LOG_ERR << "[ReportRunner] no .g files found under " << lib << LOG_ENDL;
        return 1;
    }

    for (const auto& abs : gfiles) {
        ModelData md{};
        md.short_name  = fs::path(abs).filename().string();
        // Absolute source paths let the temporary repository process the real
        // library without staging, copying, or writing beside the input files.
        md.file_path   = fs::path(abs).lexically_normal().generic_string();
        md.is_included = true;
        md.is_processed = false;
        repo.insertModel(md);
    }

    // 2) process: extract title/objects and select a primary top for each model
    {
        ProcessGFiles processor(&repo);
        const std::vector<ModelData> pending = repo.getIncludedModels();
        int done = 0;
        for (const auto& md : pending) {
            processor.processGFile(md);
            LOG_INFO << "[ReportRunner] processed " << (++done) << "/" << pending.size()
                     << ": " << md.short_name << LOG_ENDL;
        }
    }

    // 3) AI tagging (best-effort)
    const std::vector<ModelData> processed = repo.getIncludedModels();
    if (opt.tags)
        tagModels(repo, processed);

    // 4) render per-model gist pages and assemble the PDF
    SQLJobQueue queue(paths.jobsDir());
    GistHandler gist(fs::path(paths.dataDir()), nullptr, repo, queue);
    std::atomic<bool> stop{false};

    QJsonArray pages;
    int rendered = 0;
    for (const auto& md : processed) {
        const std::vector<ObjectData> sel = repo.getSelectedObjectsForModel(md.id);
        if (sel.empty()) {
            LOG_WARN << "[ReportRunner] no primary object for " << md.short_name << "; skipping" << LOG_ENDL;
            continue;
        }
        const std::string primary = sel.front().name;
        const std::string abs     = paths.resolveRelToLib(md.file_path);
        const std::string fileId  = ProcessGFiles::generateProcessDir(abs, primary);
        const fs::path    png     = fs::path(paths.dataDir()) / fileId / "gist_page.png";

        // (re)render the gist page only if we don't already have one
        if (!fs::exists(png, ec) || fs::file_size(png, ec) == 0) {
            if (!opt.render) {
                LOG_WARN << "[ReportRunner] no cached page for " << md.short_name
                         << " and rendering disabled; skipping" << LOG_ENDL;
                continue;
            }
            QJsonObject gj;
            gj["file_path"] = QString::fromStdString(abs);
            gj["primary"]   = QString::fromStdString(primary);
            // Generate low-effort geometry views first.  The final pass below
            // reuses those renders while composing gist's normal 300-PPI sheet,
            // preserving its established layout and readable metadata.
            gj["ppi"] = 72;
            gj["cpus"] = 8;
            gj["preview"] = true;
            gj["timeout_seconds"] = 45;
            if (!opt.label.empty()) gj["label"] = QString::fromStdString(opt.label);
            if (!opt.user.empty())  gj["user"]  = QString::fromStdString(opt.user);

            JobDescriptor jd;
            jd.directive  = "gist_preview";
            jd.fileId     = fileId;
            jd.sourcePath = QJsonDocument(gj).toJson(QJsonDocument::Compact).toStdString();

            LOG_INFO << "[ReportRunner] rendering low-effort gist views for " << md.short_name
                     << " (" << primary << ")" << LOG_ENDL;
            HandlerResult result = gist.handle(jd, stop);
            if (!result.success) {
                LOG_WARN << "[ReportRunner] gist failed for " << md.short_name
                         << ": " << result.message << "; skipping" << LOG_ENDL;
                continue;
            }

            gj["ppi"] = 300;
            gj["timeout_seconds"] = 120;
            jd.directive = "gist_page";
            jd.sourcePath = QJsonDocument(gj).toJson(QJsonDocument::Compact).toStdString();
            LOG_INFO << "[ReportRunner] composing standard gist sheet for " << md.short_name
                     << LOG_ENDL;
            result = gist.handle(jd, stop);
            if (!result.success) {
                LOG_WARN << "[ReportRunner] gist sheet failed for " << md.short_name
                         << ": " << result.message << "; skipping" << LOG_ENDL;
                continue;
            }
        }

        QJsonObject pg;
        pg["file_path"]  = QString::fromStdString(abs);
        pg["primary"]    = QString::fromStdString(primary);
        pg["short_name"] = QString::fromStdString(md.short_name);
        pg["long_name"]  = QString::fromStdString(md.effectiveLongName());
        pg["modelers"]   = QString::fromStdString(md.effectiveModelers());
        pg["model_type"] = QString::fromStdString(md.model_type);
        pg["tags"]       = joinTags(repo.getTagsForModel(md.id));
        pages.append(pg);
        ++rendered;
    }

    if (pages.isEmpty()) {
        LOG_ERR << "[ReportRunner] no pages could be rendered; no PDF produced" << LOG_ENDL;
        return 2;
    }

    QJsonObject job;
    job["title"]      = QString::fromStdString(opt.title);
    job["subtitle"]   = QString("Contains %1 model(s) from %2")
                            .arg(rendered).arg(QString::fromStdString(lib));
    job["version"]    = QString("Generated by CADventory %1")
                            .arg(QCoreApplication::applicationVersion());
    job["label"]      = QString::fromStdString(opt.label);
    job["user"]       = QString::fromStdString(opt.user);
    job["output_dir"] = QString::fromStdString(outPdf);
    job["pages"]      = pages;

    JobDescriptor rj;
    rj.directive  = "gist_report";
    rj.fileId     = "report_request";
    rj.sourcePath = QJsonDocument(job).toJson(QJsonDocument::Compact).toStdString();

    LOG_INFO << "[ReportRunner] assembling PDF with " << rendered << " page(s) -> " << outPdf << LOG_ENDL;
    const HandlerResult r = gist.handle(rj, stop);
    if (!r.success) {
        LOG_ERR << "[ReportRunner] PDF assembly failed: " << r.message << LOG_ENDL;
        return 2;
    }

    LOG_INFO << "[ReportRunner] report complete: " << outPdf << LOG_ENDL;
    return 0;
}
