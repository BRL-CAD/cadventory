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

    /* TODO: we'll eventually probably just want the constructor to take a library_root, 
     * and let everything else determine from there but this is easier for separating testing for now
     */
    virtual void setRootPaths(const std::string& rootDir,
                              const std::string& jobsDir = "",
                              const std::string& dataDir = "",
                              const std::string& modelRoot = "") = 0;

    virtual bool start() = 0;   // non-blocking
    virtual void stop()  = 0;   // blocking, graceful shutdown

    virtual JobServiceState state() const = 0;
    virtual JobServiceStats stats() const = 0;
};
