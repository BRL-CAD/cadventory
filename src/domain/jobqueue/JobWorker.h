#pragma once
#include "QtJobServiceBase.h"

// TODO/FIXME: use real headers
//class ModelRepository;
//class FileJobQueue;

class JobWorker : public QtJobServiceBase
{
public:
    using QtJobServiceBase::QtJobServiceBase;	    // use default constructor

protected:
    void serviceLoop() override;

private:
    ModelRepository* m_repo  = nullptr;
    FileJobQueue*    m_queue = nullptr;
};
