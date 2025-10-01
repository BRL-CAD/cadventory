#pragma once
#include <filesystem>
#include <chrono>
#include <string>

class SimpleFileLock {
public:
    using millis = std::chrono::milliseconds;

    SimpleFileLock() = default;
    explicit SimpleFileLock(std::filesystem::path lockPath,
                            millis acquireTimeout = millis{5000})
        : m_lockPath(std::move(lockPath)),
          m_timeout(acquireTimeout) {}

    void setPath(const std::filesystem::path& p) { m_lockPath = p; }
    void setAcquireTimeout(millis t)             { m_timeout  = t; }

    // RAII guard: try to acquire up to m_timeout
    class Guard {
    public:
        explicit Guard(SimpleFileLock& lock);
        ~Guard();
        bool acquired() const { return m_held; }
        explicit operator bool() const { return acquired(); }
    private:
        SimpleFileLock* m_lock = nullptr;
        bool            m_held = false;
        std::string     m_id;
        friend class SimpleFileLock;
    };

private:
    std::filesystem::path m_lockPath;
    millis m_timeout = millis{10000};   // 10s default

    // helpers
    static std::string makeLockId();
    // one-shot attempt used inside the timed loop
    bool tryAcquire(std::string& outId);
    void release(const std::string& id);
};
