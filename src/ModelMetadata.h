#ifndef MODEL_METADATA_H
#define MODEL_METADATA_H

#include <string>
#include <vector>

struct ModelMetadata {
    std::string filepath;
    std::string title;
    std::vector<std::string> objectFiles;
};

#endif // MODEL_METADATA_H
