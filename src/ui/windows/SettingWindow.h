#ifndef SETTINGWINDOW_H
#define SETTINGWINDOW_H

#include <QDialog>

namespace Ui {
class SettingWindow;
}

class SettingWindow : public QDialog
{
    Q_OBJECT

public:
    explicit SettingWindow(QWidget *parent = nullptr);
    ~SettingWindow();

private slots:
    void on_buttonBox_accepted();

    void on_buttonBox_rejected();

    void on_enablePreview_checkStateChanged(const Qt::CheckState &state);

private:
    void loadSettings();
    void saveSettings();

    Ui::SettingWindow *ui;
};

#endif // SETTINGWINDOW_H
