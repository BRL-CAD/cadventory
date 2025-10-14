#include "SimpleFileLock.h"
#include <fstream>
#include <sstream>
#include <thread>
#include <algorithm>

#include <bu/process.h>     // bu_pid()

#ifdef _WIN32
  #include <windows.h>
#else
  #include <fcntl.h>
  #include <unistd.h>
#endif

SimpleFileLock::Guard::Guard(SimpleFileLock& lock) : m_lock(&lock) {
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + m_lock->m_timeout;

    // poll backoff: start small and double with each retry up to max
    auto backoff = std::chrono::milliseconds(10);
    const auto backoffMax = std::chrono::milliseconds(160);

    while (clock::now() < deadline) {
        if (m_lock->tryAcquire(m_id)) {
            // got the lock
            m_held = true;
            return;
        }

        // didn't get it - wait and try again
        auto sleepFor = backoff;
        if (clock::now() + sleepFor > deadline)
            // will timeout
            break;
        std::this_thread::sleep_for(sleepFor);
        if (backoff < backoffMax)
            // bump backoff
            backoff *= 2;
    }

    // timed out -> m_held == false
}

SimpleFileLock::Guard::~Guard() {
    if (m_lock && m_held) {
        m_lock->release(m_id);
    }
}

bool SimpleFileLock::tryAcquire(std::string& outId) {
    const std::string id = makeLockId();
    const std::string payload = id + "\n";

#ifdef _WIN32
    HANDLE handle = CreateFileW(
        m_lockPath.wstring().c_str(),
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_NEW, // should atomically cooperate with ::open
        FILE_ATTRIBUTE_NORMAL, // test FILE_FLAG_WRITE_THROUGH to bypass disk cache
        NULL
    );
    if (handle == INVALID_HANDLE_VALUE)
        return false;

    DWORD written = 0;
    WriteFile(handle, payload.data(), (DWORD)payload.size(), &written, NULL);
    FlushFileBuffers(handle);
    CloseHandle(handle);
#else
    int fd = ::open(m_lockPath.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0666); // test O_CLOEXEC
    if (fd == -1)
        return false;

    (void)::write(fd, payload.data(), payload.size());
    (void)::fsync(fd);
    (void)::close(fd);
#endif

    // did we actually win? first line must be our id
    std::ifstream in(m_lockPath);
    std::string first;
    std::getline(in, first);
    if (first != id)
        return false;

    outId = id;
    return true;
}

void SimpleFileLock::release(const std::string& id) {
    std::string first;
    {
        // scope as stream must release before we can remove()
        std::ifstream in(m_lockPath);
        std::getline(in, first);
    }
    if (first == id) {
        std::error_code ec;
        std::filesystem::remove(m_lockPath, ec);
    }
}

std::string SimpleFileLock::makeLockId() {
    int pid = bu_pid();
    std::ostringstream thread_id;
    thread_id << std::this_thread::get_id();

    // pid-threadId
    return std::to_string(pid) + "-" + thread_id.str();
}
