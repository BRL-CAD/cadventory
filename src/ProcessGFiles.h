#ifndef PROCESSGFILES_H
#define PROCESSGFILES_H

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "Model.h"
#include <brlcad/rt/geom.h>

class ProcessGFiles {
public:
    explicit ProcessGFiles(Model* model);

    // returns true on successful processing
    bool processGFile(const ModelData& modelData);

    std::tuple<bool, std::string, std::string> generateGistReport(const std::string& inputFilePath, const std::string& outputFilePath, const std::string& primary_obj, const std::string& label);

private:
    void extractTitle(ModelData& modelData, struct ged* gedp);
    void extractObjects(ModelData& modelData, struct ged* gedp);
    void insertChildObjects(ModelData& modelData, struct ged* gedp, const ObjectData& parentObjData, const std::string& selected_object_name);

    // Thumbnail generation and command utility methods
    bool generateThumbnail(ModelData& modelData, const std::string& selected_object_name);


    Model* model;
};

void db_tree_list_comb_children(const union tree *tree, std::vector<std::string>& children);

#endif  // PROCESSGFILES_H
