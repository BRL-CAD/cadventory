#ifndef REPORTGENERATIONWINDOW_H
#define REPORTGENERATIONWINDOW_H

#include <QPainter>
#include <QPdfWriter>
#include <QThread>
#include <QWidget>

#include "ReportGeneratorWorker.h"
#include "Library.h"
#include "Model.h"
#include "ProcessGFiles.h"
#define A4_MAXWIDTH_LS 3508
#define A4_MAXHEIGHT_LS 2480


namespace Ui {
class ReportGenerationWindow;
}

class ReportGenerationWindow : public QWidget {
  Q_OBJECT

 public:
  explicit ReportGenerationWindow(QWidget* parent = nullptr,
                                  Model* model = nullptr,
                                  Library* library = nullptr);
  ~ReportGenerationWindow();

 private slots:
  void onPauseReportButtonClicked();
  void onGenerateReportButtonClicked();
  void onOutputDirectoryButtonClicked();
  void onLogo1ButtonClicked();
  void onLogo2ButtonClicked();
  void onProcessingGistCall(const QString& file);
  void onSuccessfulGistCall(const QString& path_gist_output);
  void onFailedGistCall(const QString& filepath, const QString& errorMessage, const QString& command);
  void onFinishedGeneratingReport();

 private:
  void coverPage();
  void tableOfContentsPage();

  int* num_file; // tracks progress, incremented each time a model is processed
  int* tot_num_files;
  int x = 325; // table of contents starting x
  int y = 400; // table of contents starting y
  std::string time;
  QPdfWriter* pdfWriter;
  QPainter* painter;
  Library* library;
  Model* model;
  Ui::ReportGenerationWindow* ui;
  ReportGeneratorWorker* reporterWorker;
  QThread* generatingReportThread;
  std::string subtitle;
  std::string label;
  std::string output_directory;
  std::string logo1_filepath; // top logo
  std::string logo2_filepath; // bottom logo
  std::string title;
  std::string username;
  std::string version;
  std::vector<std::string>* err_vec; // vector of errors to display
};

#endif  // REPORTGENERATIONWINDOW_H
