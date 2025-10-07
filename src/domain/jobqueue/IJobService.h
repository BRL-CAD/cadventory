#pragma once
#include <string>
#include <chrono>

enum class JobServiceState { Stopped, Starting, Running, Stopping, Error };

struct JobServiceStats {
    // TODO: make this struct more flexible / generic
    std::size_t jobsNew         = 0;
    std::size_t jobsClaimed     = 0;
    std::size_t modelsProcessed = 0;

    std::string lastError;
    std::chrono::seconds uptime {0};
};

class IJobService
{
public:
    virtual ~IJobService() = default;

    virtual void setRootPath(const std::string& path) = 0;  // library root

    virtual bool start() = 0;   // non-blocking
    virtual void stop()  = 0;   // blocking, graceful shutdown

    virtual JobServiceState state() const = 0;
    virtual JobServiceStats stats() const = 0;
};
