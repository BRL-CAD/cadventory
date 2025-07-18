#ifndef PROCESSGFILES_H
#define PROCESSGFILES_H

#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include <optional>

#include "Model.h"
#include <brlcad/rt/geom.h>

class ProcessGFiles {
public:
    explicit ProcessGFiles(Model* model);

    // return final ModelData after processing (or std::nullopt)
    std::optional<ModelData> processGFile(const ModelData& modelData);

    // all extra / heavy processes that are needed to consider the file 'completely' processed
    // i.e. thumbnail, gist, etc.
    std::vector<std::string> getAllNeededDirectives();

    std::tuple<bool, std::string, std::string> generateGistReport(const std::string& inputFilePath, const std::string& outputFilePath, const std::string& primary_obj, const std::string& label);

private:
    void extractTitle(ModelData& modelData, struct ged* gedp);
    void extractObjects(ModelData& modelData, struct ged* gedp);
    void insertChildObjects(ModelData& modelData, struct ged* gedp, const ObjectData& parentObjData, const std::string& selected_object_name);

    // Thumbnail generation and command utility methods
    bool generateThumbnail(ModelData& modelData, const std::string& selected_object_name);

    std::string generateUUID(struct ged* gedp, const std::string& objName);


    Model* model;
};

void db_tree_list_comb_children(const union tree *tree, std::vector<std::string>& children);

#endif  // PROCESSGFILES_H
