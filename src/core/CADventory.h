#ifndef CADVENTORY_H
#define CADVENTORY_H

#include <QObject>
#include <QMainWindow>
#include <QSplashScreen>
#include <memory>

#include "ILLMService.h"
#include "AIModelTagging.h"


class CADventory : public QObject
{
    Q_OBJECT

public:
    CADventory(int &argc, char *argv[], QObject* parent = nullptr);
    ~CADventory();
    static CADventory* instance() { return s_instance; }

    void showSplash();
    void run();		// kicks off file indexing

    AIModelTagging* getTagger() const { return tagger.get(); }

signals:
  void indexingComplete(const char *summary);

private slots:
    void indexDirectory(const char *path);

private:
    // helper functions
    void initMainWindow();

    // singleton
    inline static CADventory* s_instance = nullptr;

    // services
    std::unique_ptr<ILLMService>    llm;
    std::unique_ptr<AIModelTagging> tagger;

    // state
    QMainWindow* window = nullptr;
    QWidget* splash = nullptr;
    bool loaded = false;
    bool gui = true;		// default gui enabled
};

#endif /* CADVENTORY_H */
