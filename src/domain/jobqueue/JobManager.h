#pragma once

#include "QtJobServiceBase.h"
//#include "FilesystemIndexer.h"
//#include "IModelRepository.h"

// TODO/FIXME: dummy declarations - replace w/ headers
class FilesystemIndexer;
class ModelRepository;

class JobManager : public QtJobServiceBase
{
public:
    using QtJobServiceBase::QtJobServiceBase;	    // use default constructor

protected:
    void serviceLoop() override;

private:
    FilesystemIndexer* m_indexer = nullptr;
    ModelRepository*   m_repo    = nullptr;
};
