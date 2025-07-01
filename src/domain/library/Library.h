// Library.h

#ifndef LIBRARY_H
#define LIBRARY_H

#include <string>
#include <vector>
#include "FilesystemIndexer.h"
#include "Model.h"

class Library {
public:
    explicit Library(const char* label = nullptr, const char* path = nullptr);
    Library(const Library&) = delete;
    ~Library();

    size_t indexFiles();
    const char* name();
    const char* path();

    void loadDatabase();
    std::vector<std::string> getModels();
    std::vector<std::string> getGeometry();
    std::vector<std::string> getImages();
    std::vector<std::string> getDocuments();
    std::vector<std::string> getData();

    std::string shortName;
    std::string fullPath;
    Model* model;

private:
    FilesystemIndexer* index;
};

#endif // LIBRARY_H
