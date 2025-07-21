#pragma once

#include "QtJobServiceBase.h"


class JobManager : public QtJobServiceBase
{
public:
    using QtJobServiceBase::QtJobServiceBase;	    // use default constructor

protected:
    void serviceLoop() override;

private:
};
