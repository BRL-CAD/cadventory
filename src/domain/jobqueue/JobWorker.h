#pragma once

#include "QtJobServiceBase.h"

class JobWorker : public QtJobServiceBase
{
public:
    using QtJobServiceBase::QtJobServiceBase;	    // use default constructor

protected:
    void serviceLoop() override;

private:
    // helper functions
};
