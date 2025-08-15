#include "SettingWindow.h"
#include "ui_SettingWindow.h"
#include <QSettings>
#include <algorithm>

SettingWindow::SettingWindow(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SettingWindow)
{
    ui->setupUi(this);

    // Load settings when the window is initialized
    loadSettings();
}

SettingWindow::~SettingWindow()
{
    delete ui;
}

void SettingWindow::loadSettings()
{
    QSettings settings;
    bool previewFlag = settings.value("previewFlag", true).toBool();
    int previewLimit = settings.value("previewTimer", 30).toInt();
    ui->enablePreview->setChecked(previewFlag);
    ui->previewTimer->setRange(0,2400);
    ui->previewTimer->setSingleStep(10);
    ui->previewTimer->setValue(previewLimit);

    // set on command line?
    const int configured = settings.value("jobs/numThreads", 1).toInt();

    const int maxThreads = 4096;
    ui->workerThreads->setRange(1, maxThreads);
    ui->workerThreads->setValue(std::max(1, configured));
}

void SettingWindow::saveSettings()
{
    QSettings settings;
    settings.setValue("previewFlag", ui->enablePreview->isChecked());
    if(ui->enablePreview->isChecked()){
    settings.setValue("previewTimer", ui->previewTimer->value());
    }
    settings.setValue("jobs/numThreads", ui->workerThreads->value());
}

void SettingWindow::on_buttonBox_accepted()
{
    saveSettings();
    accept();
}

void SettingWindow::on_buttonBox_rejected()
{
    loadSettings();
    reject();
}


void SettingWindow::on_enablePreview_checkStateChanged(const Qt::CheckState &state)
{
    if(state == Qt::Unchecked){
        ui->previewWidget->setDisabled(true);
    }
    else if(state == Qt::Checked){
        ui->previewWidget->setDisabled(false);
    }
}

