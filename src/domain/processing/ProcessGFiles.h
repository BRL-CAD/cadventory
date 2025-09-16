#ifndef PROCESSGFILES_H
#define PROCESSGFILES_H

#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include <optional>

#include "Model.h"
#include <brlcad/ged.h>		// for directory
#include <brlcad/rt/geom.h>

class ProcessGFiles {
public:
    explicit ProcessGFiles(Model* model);

    // return final ModelData after processing (or std::nullopt)
    std::optional<ModelData> processGFile(const ModelData& modelData);

    // all extra / heavy processes that are needed to consider the file 'completely' processed
    // i.e. thumbnail, gist, etc.
    std::vector<std::string> getAllNeededDirectives();

    // creates process relative dir in pattern <bin>/<filehash>/<objName> -> ab/abc123/all.g/
    static std::string generateProcessDir(const std::string& file, const std::string& objName);

    std::tuple<bool, std::string, std::string> generateGistReport(const std::string& inputFilePath, const std::string& outputFilePath, const std::string& primary_obj, const std::string& label);

private:
    void extractTitle(ModelData& modelData, struct ged* gedp);
    void extractObjects(ModelData& modelData, struct ged* gedp, std::string& selected_object_name);

    struct WalkCtx;	// forward declare, .cpp actually implements
    // BFS walker: inserts children under parent
    void insertChildObjects(int parentId, const directory* parentDp, const WalkCtx& ctx);

    // either create or update modelData in the Model repo
    bool updateModelData(ModelData& modelData);

    // Thumbnail generation and command utility methods
    bool generateThumbnail(ModelData& modelData, const std::string& selected_object_name);



    Model* model;
};

#endif  // PROCESSGFILES_H
