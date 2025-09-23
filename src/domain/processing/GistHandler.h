#pragma once

#include <atomic>
#include <algorithm>
#include <string>
#include <vector>
#include <cctype>
#include <filesystem>

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QString>

#if CADVENTORY_WITH_GUI
#include <QImage>
#include <QPainter>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#endif

#include "IDirectiveHandler.h"
#include "SQLJobQueue.h"
#include "QtJobServiceBase.h"
#include "Model.h"
#include "ProcessGFiles.h"
#include "Logger.h"

#define FONT "Arial"

namespace fs = std::filesystem;

class GistHandler final : public IDirectiveHandler {
public:
  inline GistHandler(const fs::path& dataRoot,
                     QtJobServiceBase* service,
                     Model& repo,
                     SQLJobQueue& queue)
    : IDirectiveHandler(dataRoot, service, repo)
    , m_queue(queue) {}

  inline HandlerResult handle(const JobDescriptor& jd, std::atomic<bool>& stopFlag) override {
    if (jd.directive == "gist_page")
        return handleGist(jd, stopFlag);
    if (jd.directive == "gist_report") {
#if CADVENTORY_WITH_GUI
        return handleReport(jd, stopFlag);
#else
        return {false, "requeue"};
#endif
    }

    return {false, "unknown directive"};
  }

private:
  inline HandlerResult handleGist(const JobDescriptor& job, std::atomic<bool>& stopFlag) {
    // check for gist executable
    QString gistExecutable = QStringLiteral(GIST_EXECUTABLE_PATH);
    if (gistExecutable.isEmpty())
        return {false, "Cannot find gist executable"};

    // convenience: extract from context and job
    const auto json = parseJson(job.sourcePath);
    const QString inputFilePath = json.value("file_path").toString();
    const QString primary_obj = json.value("primary").toString();
    const fs::path outDir = _dataDir / job.fileId;
    const QString outputFilePath = QString::fromStdString(fs::path(outDir / (job.directive + ".png")).generic_string());
    const QString cache_path = QString::fromStdString(fs::path(outDir / "gist_cache").generic_string());
    // build up arguments list
    QStringList arguments;
    arguments << inputFilePath
              << "-o" << outputFilePath;
    // force re-generation
    arguments << "-f";
    // re-use previous renders if found
    arguments << "-Z";
    // supplied primary 'top' object
    arguments << "-t" << primary_obj;
    // point to consistent cache dir
    arguments << "-a" << cache_path;
    // optional arguments (like label, owner, classification, ...)
    if (json.contains("label"))
        arguments << "-c" << json.value("label").toString();
    if (json.contains("user")) {
        arguments << "-n" << json.value("user").toString();
        arguments << "-r" << json.value("user").toString();
    }
    if (json.contains("logo1"))
        arguments << "-m" << json.value("logo1").toString();


    // build our process to be run
    QProcess process;
    process.setProgram(gistExecutable);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::MergedChannels);    // merge stdout and stderr log

    // start
    process.start();
    if (!process.waitForStarted())
        return {false, "Failed to start the gist process"};

    // poll process for completion / timeout / stopFlag
    bool forceStop = false;
    bool timedOut = false;
    QSettings settings;
    // use our previewTimer since it's wired into the ui (TODO: do we want separte gist / rt setting?)
    int timeoutMs = settings.value("previewTimer", 120).toInt() * 1000;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (true) {
      // finished?
      if (process.waitForFinished(250))
          break;

      // stopFlag?
      if (stopFlag.load()) {
        process.kill();
        process.waitForFinished();
        forceStop = true;
        break;
      }

      // timeout?
      if (timeoutMs && std::chrono::steady_clock::now() >= deadline) {
        process.kill();
        process.waitForFinished();
        timedOut = true;
        break;
      }
    }

    // verify we finished successfully
    bool normalExit = (process.exitStatus() == QProcess::NormalExit);
    int exitCode = process.exitCode();
    bool outExists = (QFile::exists(outputFilePath) && QFileInfo(outputFilePath).size());
    if (forceStop || timedOut || !normalExit || exitCode != 0 || !outExists) {
        // if we failed, give an informative error message
        QString reason;
        if (forceStop)
            reason = "(stop requested)";
        else if (timedOut)
            reason = "(timed out)";
        else if (!normalExit || exitCode != 0)
            reason = "(abnormal exit: " + QString::number(exitCode) + ")";
        else if (!outExists)
            reason = "(no output found)";

        LOG_ERR << "[GistHandler] gist FAILED " + reason << "\n"
                << "  cmd:      " << gistExecutable + arguments.join(' ') << "\n"
                << "  exitCode: " << exitCode << "\n"
                << "  qError:   " << process.errorString().toStdString() << "\n"
                << LOG_ENDL;

        return {false, "gist process did not finish " + reason.toStdString()};
    }

    // success
    return {true, {}};
  }

#if CADVENTORY_WITH_GUI
  struct Page { std::string file_path, primary, short_name; };
  inline HandlerResult handleReport(const JobDescriptor& job, std::atomic<bool>& stopFlag) {
    /*
     * expecting sourcePath json:
     * {
     *   "title": "...",
     *   "user": "...",
     *   "label": "...",
     *   "subtitle": "...",
     *   "version": "...",
     *   "logo1": "...",
     *   "logo2": "...",
     *   "output_dir": "...",
     *   "pages": [
     *     { "file_path": "/abs/to/file.g", "primary": "obj", "short_name": "file.g" },
     *     ...
     *   ]
     * }
     */
    const auto json = parseJson(job.sourcePath);
    const auto pagesJson = json.value("pages").toArray();
    if (pagesJson.isEmpty())
        return {false, "no pages for report"};

    // convert json -> pages vector
    std::vector<Page> pages;
    pages.reserve(pagesJson.size());
    for (const auto& v : pagesJson) {
      const auto o = v.toObject();
      Page pg{
        o.value("file_path").toString().toStdString(),
        o.value("primary").toString().toStdString(),
        o.value("short_name").toString().toStdString()
      };
      if (!pg.file_path.empty() && !pg.primary.empty())
        pages.push_back(std::move(pg));
    }
    if (pages.empty())
        return {false, "no valid pages"};

    // case-insensitive sort by short_name, then file_path
    std::sort(pages.begin(), pages.end(), [](const Page& a, const Page& b) {
      auto as=a.short_name, bs=b.short_name;
      std::transform(as.begin(), as.end(), as.begin(), ::tolower);
      std::transform(bs.begin(), bs.end(), bs.begin(), ::tolower);
      if (as != bs) return as < bs;
      return a.file_path < b.file_path;
    });

    // ensure each page PNG exists
    std::vector<Page> missing;
    missing.reserve(pages.size());
    for (const auto& pg : pages) {
      const std::string rel = ProcessGFiles::generateProcessDir(pg.file_path, pg.primary); // ha/hash/primary
      const auto png = _dataDir / rel / "gist_page.png";        // NOTE: gist_page.png MUST match createJob name
      if (!fs::exists(png) || fs::file_size(png) == 0)
        missing.push_back(pg);
    }
    // if missing, enqueue gist job and re-enqueue this report job
    if (!missing.empty()) {
      for (const auto& pg : missing) {
        // for gist jobs, sourcePath must be the file path so needsHandled() can find the model
        std::string fileIdDir = ProcessGFiles::generateProcessDir(pg.file_path, pg.primary); // aa/hash/primary

        // build page custom json object for this job
        QJsonObject gistJson;
        gistJson["file_path"] = QString::fromStdString(pg.file_path);
        gistJson["primary"] = QString::fromStdString(pg.primary);
        gistJson["label"] = json.value("label").toString();
        gistJson["user"] = json.value("user");
        gistJson["logo1"] = json.value("logo1");

        const std::string payload = QJsonDocument(gistJson).toJson(QJsonDocument::Compact).toStdString();

        (void)m_queue.createJob(fileIdDir, "gist_page", payload);
      }
      
      // hold the job for a while so we dont constantly hammer claim job while pages are populating
      auto holdDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
      while (!stopFlag) {
          if (std::chrono::steady_clock::now() >= holdDeadline)
              break;
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
      // IMPORTANT: JobWorker is in charge of making sure this report job can get claimed again
      return {false, "requeue"};
    }

    // start actually assembling the PDF
    const std::string outPdf = fs::path(json.value("output_dir").toString().toStdString()).generic_string();
    QPdfWriter pdf(QString::fromStdString(outPdf));
    pdf.setResolution(300);
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setPageOrientation(QPageLayout::Landscape);
    pdf.setPageMargins(QMarginsF(0,0,0,0));

    QPainter painter(&pdf);
    if (!painter.isActive())
        return {false, "failed to start PDF painter"};

    // Cover
    drawCoverPage(pdf, painter, json);
    //drawPageNumber(painter, pdf, 1);  // draw page number for cover?

    // TOC
    const QString label = json.value("label").toString();
    const int tocPages = tableOfContentsPage(pdf, painter, label, pages);

    // Gist pages
    int pageNo = 1 + tocPages + 1;      // cover + toc + 1-index
    for (const auto& pg : pages) {
      std::string rel = ProcessGFiles::generateProcessDir(pg.file_path, pg.primary);
      const auto png = _dataDir / rel / "gist_page.png";

      pdf.newPage();
      painter.fillRect(QRectF(0,0,pdf.width(),pdf.height()), Qt::white);

      if (QFileInfo::exists(QString::fromStdString(png.string()))) {
        QImage img(QString::fromStdString(png.string()));
        painter.drawImage(QRect(0,0,pdf.width(),pdf.height()), img);
      } else {
        painter.drawText(QRectF(0,0,pdf.width(),pdf.height()), Qt::AlignCenter,
                         QStringLiteral("(missing gist.png)\n%1")
                           .arg(QString::fromStdString(pg.short_name)));
      }

      drawPageNumber(painter, pdf, pageNo++, Qt::white);   // draw in white on-top of bottom banner
    }

    painter.end();

    return {true, {}};
  }

 inline void drawCoverPage(QPdfWriter& pdf, QPainter& painter, const QJsonObject& json)
{
  const QString title    = json.value("title").toString();
  const QString user     = json.value("user").toString();
  const QString label    = json.value("label").toString();
  const QString subtitle = json.value("subtitle").toString();
  const QString version  = json.value("version").toString();
  const QString logoTop  = json.value("logo1").toString();
  const QString logoBot  = json.value("logo2").toString();

  const int W = pdf.width();
  const int H = pdf.height();

  // Margin relative to page size (works at any DPI): ~8% of the shortest edge, clamped to [120, 360]
  const int M = std::clamp(std::min(W, H) / 12, 120, 360);

  painter.fillRect(QRectF(0,0,W,H), Qt::white);

  auto drawLogo = [&](const QString& path, const QPoint topLeft, const QSize maxSz) {
    if (path.isEmpty()) return;
    QPixmap pm(path);
    if (pm.isNull()) return;
    QSize sz = pm.size();
    sz.scale(maxSz, Qt::KeepAspectRatio);
    painter.drawPixmap(QRect(topLeft, sz), pm);
  };

  // Logos
  drawLogo(logoTop, QPoint(M, M), QSize(W/6, H/8));
  if (!logoBot.isEmpty()) {
    QPixmap pm(logoBot);
    if (!pm.isNull()) {
      QSize sz = pm.size();
      sz.scale(QSize(W/6, H/8), Qt::KeepAspectRatio);
      painter.drawPixmap(QRect(QPoint(W - M - sz.width(), H - M - sz.height()), sz), pm);
    }
  }

  // Title block area
  QRect titleRect(M + W/8, M + H/8, W - 2*(M + W/8), H/2 - (M + H/10));
  painter.setPen(QPen(Qt::black, 2));
  { QFont f = painter.font(); f.setPointSize(32); f.setBold(true); painter.setFont(f); }
  painter.drawText(titleRect, Qt::AlignHCenter | Qt::AlignBottom | Qt::TextWordWrap, title);

  // Subtitle just below title
  QRect subtitleRect(titleRect.left(), titleRect.bottom() + H/60, titleRect.width(), H/18);
  painter.setPen(Qt::gray);
  { QFont f; f.setPointSize(16); painter.setFont(f); }
  if (!subtitle.isEmpty())
    painter.drawText(subtitleRect, Qt::AlignCenter, subtitle);

  // Label
  if (!label.isEmpty()) {
    QRect labelRect(titleRect.left(), subtitleRect.bottom() + H/100, titleRect.width(), H/18);
    painter.setPen(Qt::black);
    { QFont f; f.setPointSize(14); painter.setFont(f); }
    painter.drawText(labelRect, Qt::AlignCenter, label);
  }

  // Version bottom-left
  if (!version.isEmpty()) {
    { QFont f; f.setPointSize(12); painter.setFont(f); painter.setPen(Qt::black); }
    painter.drawText(M, H - M, version);
  }

  // Username & date top-right
  {
    { QFont f; f.setPointSize(12); painter.setFont(f); painter.setPen(Qt::black); }
    const QRect userRect(W - M - W/6, M, W/6, H/20);
    if (!user.isEmpty())
      painter.drawText(userRect, Qt::AlignRight | Qt::TextWordWrap, user);

    const QRect dateRect(W - M - W/6, M + H/20, W/6, H/20);
    const QString dateStr = QDateTime::currentDateTime().toString("MM-dd-yyyy, HH:mm:ss");
    painter.drawText(dateRect, Qt::AlignRight, dateStr);
  }
}

inline int tableOfContentsPage(QPdfWriter& pdf,
                               QPainter& painter,
                               const QString& label,
                               const std::vector<Page>& rows,
                               int coverPageNumber = 1)
{
  const int W = pdf.width();
  const int H = pdf.height();
  const int margin = std::clamp(std::min(W, H) / 12, 120, 360);
  const int titleH = std::clamp(H / 18, 80, 160);
  const int gap = std::clamp(H / 24, 40, 120);
  const QRect titleRect(W/2 - W/6, margin, W/3, titleH);

  const int x_tb = margin;
  const int y_tb = margin + titleH + gap;
  const int width_tb  = W - 2*margin;
  const int height_tb = H - y_tb - margin;
  const QRect tableRect(x_tb, y_tb, width_tb, height_tb);

  // Columns
  const int colPageW = std::clamp(width_tb / 10, 120, 260);
  const int colNameW = std::clamp(width_tb / 3,  300, 1000);
  const int colLongW = width_tb - colPageW - colNameW;

  const int xPage = x_tb;
  const int xName = xPage + colPageW;
  const int xLong = xName + colNameW;

  // Rows
  const int headerH    = std::clamp(H / 18, 60, 140);
  const int rowH       = std::clamp(H / 45, 28, 70);
  const int rowsPerPg  = std::max(1, (height_tb - headerH) / rowH);
  const int headerY    = y_tb;
  const int firstRowY  = y_tb + headerH;

  // Fonts
  { QFont f(FONT, 32); painter.setFont(f); painter.setPen(QPen(Qt::black, 2)); }
  const int tocPages = std::max(1, static_cast<int>(std::ceil(rows.size() / double(rowsPerPg))));
  const int contentStartPage = coverPageNumber + tocPages + 1;

  int rowIndex = 0;
  for (int printed = 0; printed < tocPages; ++printed) {
    if (!pdf.newPage()) {
        LOG_ERR << "Error creating TOC page" << LOG_ENDL;
        break;
    }

    // Title
    painter.setFont(QFont(FONT, 32));
    painter.setPen(QPen(Qt::black, 2));
    painter.drawText(titleRect, Qt::AlignVCenter | Qt::AlignHCenter,
                     QStringLiteral("Table of Contents"));

    // label banner
    if (!label.isEmpty()) {
      painter.setPen(Qt::gray);
      painter.setFont(QFont(FONT, 18));
      painter.drawText(x_tb, margin, label);
    }

    // Table outline
    painter.setPen(QPen(Qt::black, 2));
    painter.drawRect(tableRect);

    // Header row
    painter.setPen(QPen(Qt::black, 1));
    painter.setFont(QFont(FONT, 12));

    QRect headPage(xPage, headerY, colPageW, headerH);
    QRect headName(xName, headerY, colNameW, headerH);
    QRect headLong(xLong, headerY, colLongW, headerH);

    painter.drawRect(headPage); painter.drawText(headPage, Qt::AlignCenter,  "Page");
    painter.drawRect(headName); painter.drawText(headName, Qt::AlignCenter,  "Short Name");
    painter.drawRect(headLong); painter.drawText(headLong, Qt::AlignCenter,  "Title/Long Name");

    // Footer page number for TOC page itself
    drawPageNumber(painter, pdf, coverPageNumber + 1 + printed);

    // Rows
    painter.setFont(QFont(FONT, 8));
    int y = firstRowY;
    for (int r = 0; r < rowsPerPg && rowIndex < static_cast<int>(rows.size()); ++r, ++rowIndex) {
      const auto& page = rows[rowIndex];

      QRect cellPage(xPage, y, colPageW, rowH);
      QRect cellName(xName, y, colNameW, rowH);
      QRect cellLong(xLong, y, colLongW, rowH);

      painter.drawRect(cellPage);
      painter.drawRect(cellName);
      painter.drawRect(cellLong);

      const int contentPage = contentStartPage + rowIndex;
      painter.drawText(cellPage, Qt::AlignCenter, QString::number(contentPage));
      painter.drawText(cellName, Qt::AlignCenter, QString::fromStdString(page.short_name));
      painter.drawText(cellLong, Qt::AlignCenter, QString::fromStdString(page.file_path));

      y += rowH;
    }
  }

  return tocPages;
}

  static void drawPageNumber(QPainter& painter, QPdfWriter& pdf, int pageNo, const QColor& color = Qt::black) {
      /* paint uniform page number location in bottom right */
      QFont font = painter.font();
      font.setPointSize(10);
      painter.setFont(font);
      painter.setPen(color);
      const int width = pdf.width();
      const int height = pdf.height();
      painter.drawText(width - 120, height - 60, QString::number(pageNo));
  }

#endif

  // helpers
  static inline QJsonObject parseJson(const std::string& s) {
    /* extract QJsonObject from std::string */
    if (s.empty())
        return {};
    const auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(s));
    return doc.isNull() ? QJsonObject{} : doc.object();
  }


private:
  SQLJobQueue& m_queue;
};
