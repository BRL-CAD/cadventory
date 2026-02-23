#include "FileJobQueue.h"

#include <sstream>
#include <iomanip>
#include <cstring>    // for strlen

FileJobQueue::FileJobQueue(const fs::path& libraryRoot)
    : m_libRoot(fs::canonical(libraryRoot)),
      m_jobsRoot(m_libRoot / JOBS_DIR) {
    fs::create_directories(m_jobsRoot / NEW_DIR);
    fs::create_directories(m_jobsRoot / CUR_DIR);
    fs::create_directories(m_jobsRoot / DONE_DIR);

    // prime done-cache from existing files
    for (auto const& p : fs::recursive_directory_iterator(m_jobsRoot / DONE_DIR,
                         fs::directory_options::skip_permission_denied))
    {
        if (!p.is_regular_file())
            continue;
        m_doneCache.insert(hashPath(p.path().stem().string())); // stem = file.job
    }
}

FileJobQueue::~FileJobQueue() {
    janitorDirs();
    pruneEmptyDirs();
}

bool FileJobQueue::enqueueJob(const fs::path& absFilename) {
    const std::string key = hashPath(absFilename.string());

    {
        std::lock_guard lk(m_mutex);
        if (m_doneCache.count(key))           // already processed
            return false;
    }

    fs::path jp = m_jobsRoot / NEW_DIR /
                  hashDirs(absFilename.string()) /
                  absFilename.filename();
    jp += ".job";

    if (fs::exists(jp))                       // already queued
        return false;

    fs::create_directories(jp.parent_path());

    std::ofstream ofs(jp, std::ios::out | std::ios::binary);
    if (!ofs)
        // raced (and lost)
        return false;

    // write our filename
    ofs << absFilename.string();
    ofs.close();

    {
        std::lock_guard lk(m_mutex);
        m_jobMap[jp] = absFilename;
    }
    return true;
}

bool FileJobQueue::createJob(const std::string& jobId, const std::string& jobType) {
    fs::path rel = jobId + "." + jobType;
    return enqueueJob(rel);
}

std::optional<ClaimedJob> FileJobQueue::takeJob() {
    std::vector<ClaimedJob> tmp;
    if (takeBatch(1, tmp) == 1)
        return tmp.back();

    return std::nullopt;
}

std::size_t FileJobQueue::takeBatch(std::size_t maxJobs, std::vector<ClaimedJob>& out) {
    std::size_t claimed = 0;
    const fs::path newRoot = m_jobsRoot / NEW_DIR;

    for (auto it = fs::recursive_directory_iterator(newRoot,
                   fs::directory_options::skip_permission_denied);
         it != fs::recursive_directory_iterator{} && claimed < maxJobs;
         ++it)
    {
        if (!it->is_regular_file())
            continue;
        const fs::path job = it->path();

        fs::path orig;
        {
            std::lock_guard lk(m_mutex);
            auto jt = m_jobMap.find(job);
            if (jt != m_jobMap.end()) orig = jt->second;
        }

        if (orig.empty()) {
            std::ifstream ifs(job, std::ios::in | std::ios::binary);
            if (!ifs)
                // couldnt read; move on
                continue;
            std::string line;
            std::getline(ifs, line);
            orig = line;
        }

        if (m_doneCache.count(hashPath(orig.string()))) {
            // alreaady finished
            fs::remove(job);
            continue;
        }

        // check sibling in done/
        fs::path doneSibling = job;
        doneSibling = fs::path(doneSibling.string().replace(
                               doneSibling.string().find(NEW_DIR),
                               std::strlen(NEW_DIR), DONE_DIR));

        fs::path doneDir = doneSibling.parent_path();
        if (fs::exists(doneDir)) {
            for (auto const& f : fs::directory_iterator(doneDir))
                if (f.path().stem() == job.filename()) {
                    // already done
                    fs::remove(job);
                    continue;
                }
        }

        // claim: new -> cur
        {
            fs::path rel = fs::relative(job, newRoot);
            std::uniform_int_distribution<std::uint64_t> dist;
            std::stringstream ss; ss << '.' << std::hex << dist(m_rng);
            fs::path dst = m_jobsRoot / CUR_DIR / rel;
            dst += ss.str();

            fs::create_directories(dst.parent_path());
            std::error_code ec;
            fs::rename(job, dst, ec);
            if (ec) {   
                // lost race
                std::lock_guard lk(m_mutex);
                m_jobMap.erase(job);
                continue;
            }

            {
                std::lock_guard lk(m_mutex);
                m_jobMap.erase(job);
                m_jobMap[dst] = orig;
            }

            out.push_back({dst, orig});
            ++claimed;
        }
    }
    return claimed;
}

bool FileJobQueue::markDone(const fs::path& curJob) {
    fs::path orig;
    {
        std::lock_guard lk(m_mutex);
        auto jt = m_jobMap.find(curJob);
        if (jt == m_jobMap.end())
            // we didn't claim this?
            return false;
        orig = jt->second;
    }

    // cur -> done
    fs::path dst = curJob;
    dst = fs::path(dst.string().replace(dst.string().find(CUR_DIR), std::strlen(CUR_DIR), DONE_DIR));

    fs::create_directories(dst.parent_path());

    std::error_code ec;
    fs::rename(curJob, dst, ec);

    if (ec) {
        bool curExists  = fs::exists(curJob);
        bool doneExists = fs::exists(dst);

        if (!curExists && doneExists) {
            // someone else marked done
            std::lock_guard lk(m_mutex);
            m_doneCache.insert(hashPath(orig.string()));
            m_jobMap.erase(curJob);
            return true;
        }
        if (!curExists && !doneExists) {
            std::lock_guard lk(m_mutex);
            m_jobMap.erase(curJob);
            return false;
        }

        // i/o hitch; try again
        return markDone(curJob);
    }

    {
        std::lock_guard lk(m_mutex);
        m_jobMap.erase(curJob);
        m_doneCache.insert(hashPath(orig.string()));
    }
    return true;
}

void FileJobQueue::janitorDirs() {
    const auto now = fs::file_time_type::clock::now();
    const fs::path curRoot  = m_jobsRoot / CUR_DIR;
    const fs::path doneRoot = m_jobsRoot / DONE_DIR;

    // recycle stale cur/
    for (auto const& f : fs::recursive_directory_iterator(curRoot,
                     fs::directory_options::skip_permission_denied))
    {
        if (!f.is_regular_file())
            continue;

        auto age = now - f.last_write_time();
        if (age > CUR_JOB_TIMEOUT) {
            fs::path rel = fs::relative(f, curRoot);
            fs::path dst = m_jobsRoot / NEW_DIR / rel.stem(); // drop .rand
            fs::create_directories(dst.parent_path());
            std::error_code ec; fs::rename(f, dst, ec);
        }
    }

    // sweep a handful of done/ buckets each pass
    for (int i = 0; i < JANITOR_BUCKETS_PER_PASS; ++i) {
        uint8_t hi = (m_lastJanitorBucketIdx >> 8) & 0xFF;
        uint8_t lo =  m_lastJanitorBucketIdx       & 0xFF;

        std::stringstream ss;
        ss << std::hex << std::setw(2) << std::setfill('0') << +hi << '/'
           << std::setw(2) << +lo;

        fs::path bucket = doneRoot / ss.str();
        if (fs::exists(bucket)) {
            for (auto const& f : fs::directory_iterator(bucket)) {
                auto age = now - f.last_write_time();
                if (age > DONE_CLEANUP_TIME)
                    fs::remove(f);
            }
            std::error_code ec; fs::remove(bucket, ec); // rmdir if empty
        }
        m_lastJanitorBucketIdx = (m_lastJanitorBucketIdx + 1) & 0xFFFF;
    }
}

void FileJobQueue::pruneEmptyDirs() {
    for (auto it = fs::recursive_directory_iterator(m_jobsRoot,
                   fs::directory_options::skip_permission_denied);
         it != fs::recursive_directory_iterator{}; )
    {
        const fs::path dir = it->path();
        ++it; // increment before possible erase
        if (fs::is_directory(dir) &&
            fs::is_empty(dir) &&
            dir != m_jobsRoot)
        {
            std::error_code ec; fs::remove(dir, ec);
        }
    }
}

std::string FileJobQueue::hashPath(const std::string& s) {
    // dummy hash for testing
    std::size_t h = std::hash<std::string>{}(s);
    std::stringstream ss; ss << std::hex << h;
    return ss.str();
}

std::string FileJobQueue::hashDirs(const std::string& s) {
    // aa/bb buckets
    std::size_t h = std::hash<std::string>{}(s);
    std::stringstream ss;
    ss << std::hex << std::setw(2) << std::setfill('0') << ((h >> 8) & 0xFF)
       << '/' << std::setw(2) << (h & 0xFF);
    return ss.str();
}
