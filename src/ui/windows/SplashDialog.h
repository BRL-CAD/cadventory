#ifndef SPLASHDIALOG_H
#define SPLASHDIALOG_H

#include <QDialog>
#include <memory>

namespace Ui { class Dialog; }

class SplashDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SplashDialog(QWidget *parent = nullptr);
    ~SplashDialog();

private:
    std::unique_ptr<Ui::Dialog> dialog;
};

#endif /* SPLASHDIALOG_H */
