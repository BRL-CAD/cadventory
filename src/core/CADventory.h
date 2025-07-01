#ifndef CADVENTORY_H
#define CADVENTORY_H

#include <QObject>
#include <QMainWindow>
#include <QSplashScreen>
#include <QProcess>

#include "ModelTagging.h"


class CADventory : public QObject
{
    Q_OBJECT

public:
    CADventory(int &argc, char *argv[], QObject* parent = nullptr);
    ~CADventory();

    void showSplash();
    void run();		// kicks off file indexing

    ModelTagging* getModelTagging() { return modelTagging; }

signals:
  void indexingComplete(const char *summary);

private slots:
    void indexDirectory(const char *path);
    void checkAndSetupModels();

private:
    // helper functions
    bool startOllamaServer();
    void initMainWindow();

    // state
    ModelTagging* modelTagging = nullptr;
    QProcess* m_ollamaProcess = nullptr;
    QMainWindow* window = nullptr;
    QWidget* splash = nullptr;
    bool loaded = false;
    bool gui = false;
};

#endif /* CADVENTORY_H */
