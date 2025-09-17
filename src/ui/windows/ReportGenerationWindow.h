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
  void onGenerateReportButtonClicked();
  void onOutputDirectoryButtonClicked();
  void onLogo1ButtonClicked();
  void onLogo2ButtonClicked();

 private:
  Library* library;
  Model* model;
  Ui::ReportGenerationWindow* ui;
  std::string dotCadventory;
};

#endif  // REPORTGENERATIONWINDOW_H
