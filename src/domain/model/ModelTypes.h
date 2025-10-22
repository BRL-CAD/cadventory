#pragma once

struct ModelData {
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
  std::vector<std::string> tags;

  std::string is_processed_dir = "";
};

struct ObjectData {
  int object_id;
  int model_id;
  std::string name;
  int parent_object_id;
  bool is_selected;
};
