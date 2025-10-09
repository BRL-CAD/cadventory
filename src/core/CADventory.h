#ifndef CADVENTORY_H
#define CADVENTORY_H

#include <QObject>
#include <memory>

#include "config.h"
#include "ILLMService.h"
#include "AIModelTagging.h"
#include "IJobService.h"


class CADventory : public QObject
{
    Q_OBJECT

public:
    CADventory(int &argc, char *argv[], QObject* parent = nullptr);
    ~CADventory();
    static CADventory* instance() { return s_instance; }
    const std::string version() { return CADVENTORY_VERSION; }

    void run();

    AIModelTagging* getTagger() const { return tagger.get(); }
    IJobService* getJobService() const { return jobService.get(); }

signals:
  void indexingComplete(const char *summary);

private slots:
    //void indexDirectory(const char *path);

private:
    // helper functions
    void initMainWindow();

    // singleton
    inline static CADventory* s_instance = nullptr;

    // services
    std::unique_ptr<ILLMService>    llm;
    std::unique_ptr<AIModelTagging> tagger;
    std::unique_ptr<IJobService>    jobService;

    // config
    bool gui = true;		// default gui enabled
};

#endif /* CADVENTORY_H */
