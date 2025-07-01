#ifndef SPLASHDIALOG_H
#define SPLASHDIALOG_H

#include <QDialog>
#include <QObject>
#include "./ui_splash.h"


class SplashDialog : public QDialog
{
  Q_OBJECT

public:
  explicit SplashDialog(QWidget *parent = nullptr);
  ~SplashDialog();

private:
  Ui::Dialog dialog;
};

#endif /* SPLASHDIALOG_H */
