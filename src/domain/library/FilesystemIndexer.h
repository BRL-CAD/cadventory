#ifndef FILESYSTEMINDEXER_H
#define FILESYSTEMINDEXER_H

#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <vector>
#include <string>


class FilesystemIndexer {

public:
  explicit FilesystemIndexer(const char *rootDir = nullptr, long depth = 3);
  FilesystemIndexer(const FilesystemIndexer&) = delete;
  ~FilesystemIndexer();

  void setProgressCallback(std::function<void(const std::string&)> callback);

  // returns number of files indexed
  size_t indexDirectory(const std::string& path, long depth = 3);

  std::vector<std::string> findFilesWithSuffixes(const std::vector<std::string>& suffixes);

  size_t indexed();

private:
  std::unordered_map<std::string, std::vector<std::string>> fileIndex;
  std::unordered_set<std::string> visitedPaths;

  std::function<void(const std::string&)> callback;
};


#endif /* FILESYSTEMINDEXER_H */
