#ifndef MODELPARSER_H
#define MODELPARSER_H

#include <string>
#include <vector>

#include "ModelMetadata.h"

class ModelParser {
public:
    ModelParser();
    explicit ModelParser(std::string mgedExecutable);

    ModelMetadata parseModel(std::string filepath) const;

private:
    std::string fetchTitle(const std::string& path) const;
    std::vector<std::string> fetchObjectFiles(const std::string& path) const;

    std::string m_mgedExecutable;
};

#endif // MODELPARSER_H
