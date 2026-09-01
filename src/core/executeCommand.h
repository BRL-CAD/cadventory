#ifndef EXECUTE_COMMAND_H
#define EXECUTE_COMMAND_H

#include <atomic>
#include <chrono>
#include <string>
#include <vector>

struct ProcessResult {
    std::string output;
    std::string error;
    int exitCode = -1;
    bool started = false;
    bool timedOut = false;
    bool cancelled = false;
    bool crashed = false;

    bool success() const noexcept {
        return started && !timedOut && !cancelled && !crashed && exitCode == 0;
    }
};

ProcessResult runProcess(
    const std::string& program,
    const std::vector<std::string>& arguments,
    std::chrono::milliseconds timeout = std::chrono::seconds(30),
    const std::atomic_bool* cancellation = nullptr);

#endif // EXECUTE_COMMAND_H
