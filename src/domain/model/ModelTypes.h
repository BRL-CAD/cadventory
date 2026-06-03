#pragma once

#include <string>
#include <vector>

struct ObjectData {
    int object_id;
    int model_id;
    std::string name;
    int parent_object_id;   // -1 or NULL == parent
    bool is_selected;
};

struct ModelData {
    // 'core' model data; persists in db
    int id;
    std::string short_name;
    std::string primary_file;
    std::string override_info;
    std::string title;
    std::vector<char> thumbnail;
    std::string author;
    std::string file_path;
    std::string library_name;
    bool is_selected;
    bool is_processed;
    bool is_included;

    // associated tables
    std::vector<std::string> tags;
    std::vector<ObjectData> objects;

    // canonical metadata fields
    std::string long_name = "";
    std::string modelers = "";
    std::string model_type = "";

    // optional data; doesn't persist
    std::string is_processed_dir = "";

    std::string effectiveLongName() const {
        return long_name.empty() ? title : long_name;
    }

    std::string effectiveModelers() const {
        return modelers.empty() ? author : modelers;
    }

    void syncMetadataAliases() {
        if (long_name.empty())
            long_name = title;
        if (title.empty())
            title = long_name;

        if (modelers.empty())
            modelers = author;
        if (author.empty())
            author = modelers;
    }
};
