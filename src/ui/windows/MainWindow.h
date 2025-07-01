// MainWindow.h

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <map>
#include <string>
#include <vector>

#include <QMainWindow>
#include <QObject>

#include "ui_mainwindow.h"
#include "SettingWindow.h"

#include "Library.h"


class SettingWindow;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    void addLibrary(const char* label = nullptr, const char* path = nullptr);
    void openLibrary();
    const std::vector<Library*>& getLibraries() const { return libraries; }
    size_t publicSaveState() { return saveState(); }
    size_t publicLoadState() { return loadState(); }
    void clearLibraries();

    void showSettingsWindow();
    void setPreviewFlag(bool state);
    bool previewFlag = true;
    QMenu * fileMenu;
    QMenu * editMenu;
    QMenu * viewMenu;
    QMenu * windowMenu;
    QMenu * helpMenu;
    QMenu * removelib;
    SettingWindow *settingWindow;
    void returnCentralWidget();
    void resetting();

public slots:
    void updateStatusLabel(const char* status);
    QString getStatusLabel();

private slots:
    void on_addLibraryButton_clicked();


protected:
    size_t saveState();
    size_t loadState();

    void addLibraryButton(const char* label = nullptr, const char* path = nullptr);
    void removeLibrary();

private:
    Ui::MainWindow ui;
    std::vector<Library*> libraries;



};

#endif /* MAINWINDOW_H */
