#pragma once
#include <filesystem>
#include <string>
#include <fstream>

using std::filesystem::path;

// hidden dir names
inline constexpr const char* CADV_DOTFOLDER = ".cadventory";
inline constexpr const char* CADV_DATA_DIR  = "data";
inline constexpr const char* CADV_JOBS_DIR  = "jobs";
inline constexpr const char* CADV_JOBS_DB   = "jobs.db";
inline constexpr const char* CADV_MODEL_DB  = "metadata.db";
inline constexpr std::size_t CADV_BIN_LEN   = 2; // bin size for data/<bin>/<id>

class HiddenDir {
public:
    HiddenDir() = default;
    explicit HiddenDir(const path& libRoot) { setLibraryRoot(libRoot); }

    // root swapping
    void setLibraryRoot(const path& libRoot) {
        m_root = libRoot;
        m_dotFolder = (m_root / CADV_DOTFOLDER);
        ensureLayout();
    }

    // create relative path to library: <library>/path/to/file.g -> path/to/file.g
    std::string relativeToLibrary(const path& fullPath) const {
        return norm(fullPath.lexically_relative(m_root));
    }

    // resolve relative path to library: path/to/file.g -> <library>/path/to/file.g
    std::string resolveRelToLib(const path& rel) const {
        return norm(m_root / rel);
    }

    // main getters
    std::string libRoot()   const { return norm(m_root); }
    std::string dotFolder() const { return norm(m_dotFolder); }
    std::string dataDir()   const { return norm(m_dotFolder / CADV_DATA_DIR); }
    std::string jobsDir()   const { return norm(m_dotFolder / CADV_JOBS_DIR); }
    std::string modelDb()   const { return norm(m_dotFolder / CADV_MODEL_DB); }
    std::string jobsDb()    const { return norm(m_dotFolder / CADV_JOBS_DIR / CADV_JOBS_DB); }

    // consistent data dir layout: data/<prefix>/<id>/<filename>
    path outputPath(const std::string& id, const std::string& fileName) const {
        const std::string bin = id.substr(0, CADV_BIN_LEN);
        const path outDir = path(dataDir()) / bin / id;
        std::error_code ec; // ignore errors
        std::filesystem::create_directories(outDir, ec);    // ensure exists
        
        return norm(outDir / fileName);
    }

private:
    void ensureLayout() {
        // ensure we have all expected directories
        std::error_code ec; // ignore errors
        std::filesystem::create_directories(m_dotFolder, ec);
        std::filesystem::create_directories(dataDir(), ec);
        std::filesystem::create_directories(jobsDir(), ec);

        // ensure our files too
        const path modelDbPath = m_dotFolder / CADV_MODEL_DB;
        const path jobsDbPath = m_dotFolder / CADV_JOBS_DIR / CADV_JOBS_DB;
        std::ofstream modelDb(modelDbPath);
        std::ofstream jobDb(jobsDbPath);
    }

    static std::string norm(const path& _path) {
        // cleanup path, normalize to forward slashes
        return _path.lexically_normal().generic_string();
    }

    path m_root;           // library root
    path m_dotFolder;      // convenience: <libraryRoot>/.cadventory
};
