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

    // optional data; doesn't persist
    std::string is_processed_dir = "";
};
