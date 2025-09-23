#include "ReportGenerationWindow.h"
#include "Logger.h"

#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>

#include "SQLJobQueue.h"
#include "ui_reportgenerationwindow.h"

ReportGenerationWindow::ReportGenerationWindow(QWidget* parent, Model* model,
                                               Library* library)
    : QWidget(parent),
      library(library),
      model(model),
      ui(new Ui::ReportGenerationWindow) {
    ui->setupUi(this);

    // cache our model hidden directory
    dotCadventory = model->getHiddenDirectoryPath();

    // change placeholder text for subtitle
    // x models in report
    QString subtitle = "Contains " + QString::number((model->getSelectedModels().size())) + " models.";
    ui->subtitle_textEdit->setPlaceholderText(subtitle);

    // placeholder default output directory
    QString default_out = QString::fromStdString(dotCadventory) + "/data/gist_reports/report.png";
    ui->outputDirectory_textEdit->setPlaceholderText(default_out);

    // wire buttons
    connect(ui->outputDirectory_pushButton, &QPushButton::clicked, this, 
            &ReportGenerationWindow::onOutputDirectoryButtonClicked);
    connect(ui->logo1_pushButton, &QPushButton::clicked, this,
            &ReportGenerationWindow::onLogo1ButtonClicked);
    connect(ui->logo2_pushButton, &QPushButton::clicked, this,
            &ReportGenerationWindow::onLogo2ButtonClicked);
    connect(ui->generateReport_pushButton, &QPushButton::clicked, this,
            &ReportGenerationWindow::onGenerateReportButtonClicked);
}

// initiate generate report procedure
void ReportGenerationWindow::onGenerateReportButtonClicked() {
    // require at least one selection
    const auto selected = model->getSelectedModels();
    if (selected.empty()) {
        QMessageBox::warning(this, "No selection", "Select at least one model to include.");
        return;
    }

    // build pages json array
    QJsonArray pages;
    for (const auto& md : selected) {
        const auto objs = model->getSelectedObjectsForModel(md.id);
        if (objs.empty()) {
            // not sure how we got a model without a top object; fail silently
            LOG_WARN << "[ReportGenerationWindow] skipping model without a selected top: " << md.file_path << LOG_ENDL;
            continue;
        }

        QJsonObject pg;
        pg["file_path"]  = QString::fromStdString(md.file_path);
        pg["primary"]    = QString::fromStdString(objs.front().name);
        pg["short_name"] = QString::fromStdString(md.short_name);
        // TODO: do we care about anything else for the report/job - it's easier & faster to stuff it here
        pages.append(pg);
    }
    if (pages.isEmpty()) {
        QMessageBox::warning(this, "Nothing to do", "None of the selected models has a chosen top object.");
        return;
    }

    // assemble job json - gather fields from UI
    QJsonObject job;
    job["title"]      = ui->title_textEdit->toPlainText().trimmed();
    job["user"]       = ui->username_textEdit->toPlainText().trimmed();
    job["label"]      = ui->label_textEdit->toPlainText().trimmed();
    job["subtitle"]   = ui->subtitle_textEdit->toPlainText().trimmed();
    job["version"]    = ui->version_textEdit->toPlainText().trimmed();
    job["logo1"]      = ui->logo1_textEdit->toPlainText().trimmed();
    job["logo2"]      = ui->logo2_textEdit->toPlainText().trimmed();
    job["output_dir"] = ui->outputDirectory_textEdit->toPlainText().trimmed();
    job["pages"]      = pages;

    const QString timestamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss");

    // output is not optional; if we didn't get one create a timestamped file
    if (ui->outputDirectory_textEdit->toPlainText().isEmpty()) {
        const QString default_out = QString::fromStdString(dotCadventory) + 
                                    QString("/data/gist_reports/") + 
                                    QString("report_%1.pdf").arg(timestamp);
        job["output_dir"] = default_out;
    }

    // create a job for this report
    const auto jobsDir = (std::filesystem::path(dotCadventory) / "jobsdb").string();
    SQLJobQueue queue(jobsDir);
    // use timestamp for a unique key file_id
    const QString reqKey = QString("report_request:%1").arg(timestamp);
    const std::string payload = QJsonDocument(job).toJson(QJsonDocument::Compact).toStdString();

    if (!queue.createJob(reqKey.toStdString(), "gist_report", payload)) {
        QMessageBox::critical(this, "Enqueue failed", "Could not enqueue gist_report job.");
        return;
    }

    ui->generateReport_pushButton->setEnabled(false);
}

ReportGenerationWindow::~ReportGenerationWindow() {
    delete ui;
}

void ReportGenerationWindow::onOutputDirectoryButtonClicked() {
    QFileDialog dlg(this, tr("Save report as"));
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setFileMode(QFileDialog::AnyFile);
    dlg.setNameFilters({tr("PDF Files (*.pdf)"), tr("All Files (*.*)")});
    dlg.setDefaultSuffix("pdf");
    dlg.selectFile("gist_report.pdf");
    dlg.setDirectory(ui->outputDirectory_textEdit->toPlainText().trimmed());

    if (!dlg.exec())
        return;

    QString file = dlg.selectedFiles().value(0);
    if (file.isEmpty())
        return;
    if (!file.endsWith(".pdf", Qt::CaseInsensitive))
        file += ".pdf";

    // show it in your UI (replace with your actual widget)
    ui->outputDirectory_textEdit->setPlainText(QDir::toNativeSeparators(file));
}

// open file explorer to choose top logo 
void ReportGenerationWindow::onLogo1ButtonClicked() {
  QString top_logo_path = QFileDialog::getOpenFileName(this, tr("Choose first logo for report."),
                                                       "", tr("Images (*.png *.xpm *.jpg)"));
  ui->logo1_textEdit->setPlainText(top_logo_path);
}
// open file explorer to choose bottom logo
void ReportGenerationWindow::onLogo2ButtonClicked() {
  QString bottom_logo_path = QFileDialog::getOpenFileName(this, tr("Choose second logo for report."),
                                                          "", tr("Images (*.png *.xpm *.jpg)"));
  ui->logo2_textEdit->setPlainText(bottom_logo_path);
}
