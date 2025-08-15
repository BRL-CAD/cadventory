// Library.cpp

#include "Library.h"
#include "ProcessGFiles.h"

#include <set>
#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <filesystem>

namespace fs = std::filesystem;

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return char(std::tolower(c)); });
    return s;
}
static std::string normExt(const std::string& e) {
    if (e.empty())
        return e;
    if (e[0] == '.')
        return toLower(e);
    return "." + toLower(e);
}

Library::Library(const char* _label, const char* _path)
    : shortName(_label ? _label : ""),
    fullPath(_path ? _path : ""),
    model(new Model(_path))
{
}

Library::~Library()
{
    delete model;
}

const char* Library::name()
{
    return shortName.c_str();
}

const char* Library::path()
{
    return fullPath.c_str();
}

size_t Library::indexFiles()
{
    loadDatabase();

    return static_cast<size_t>(model->rowCount());
}

void Library::loadDatabase()
{
    model->refreshModelData();
}

std::vector<std::string> Library::findFilesWithSuffixes(
    const std::vector<std::string>& suffixes,
    bool onlyIncluded)
{
    loadDatabase();

    // normalize suffix list to lowercase ".ext"
    std::set<std::string> wanted;
    for (auto s : suffixes)
        wanted.insert(normExt(s));

    // get rows from db
    std::vector<ModelData> rows;
    rows = onlyIncluded ? model->getIncludedModels() : model->getAll();

    std::set<std::string> uniqueRel;
    for (const auto& m : rows) {
        fs::path p(m.file_path);
        std::string ext = p.has_extension() ? toLower(p.extension().string()) : std::string{};
        if (!wanted.empty() && !wanted.count(ext))
            continue;

        std::error_code ec;
        fs::path rel = fs::relative(p, fullPath, ec);
        const std::string out = ec ? p.generic_string() : rel.generic_string();
        uniqueRel.insert(out);
    }

    return std::vector<std::string>(uniqueRel.begin(), uniqueRel.end());
}

std::vector<std::string> Library::getModels()
{
    /* Care about files with a .g extension */
    std::vector<std::string> modelSuffixes = {".g"};
    return findFilesWithSuffixes(modelSuffixes);
}

std::vector<std::string> Library::getGeometry()
{
    std::vector<std::string> geometrySuffixes = {
        ".3dm", ".3ds", ".3mf", ".amf", ".asc", ".asm", ".brep", ".c4d",
        ".cad", ".catpart", ".catproduct", ".cfdesign", ".dae", ".drw",
        ".dwg", ".dxf", ".easm", ".fbx", ".fcstd", ".g", ".glb", ".gltf",
        ".iam", ".ifc", ".iges", ".igs", ".ipt", ".jt", ".mgx", ".nx",
        ".obj", ".par", ".ply", ".prt", ".rvt", ".sab", ".sat", ".scad",
        ".scdoc", ".skp", ".sldasm", ".slddrw", ".sldprt", ".step", ".stl",
        ".stp", ".u3d", ".vda", ".wrp", ".x_b", ".x_t", ".zpr", ".zzzgeo"
    };
    return findFilesWithSuffixes(geometrySuffixes);
}

std::vector<std::string> Library::getImages()
{
    std::vector<std::string> imageSuffixes = {
        ".bmp", ".bw", ".cgm", ".dds", ".dpx", ".exr", ".gif", ".hdr",
        ".jpeg", ".jpg", ".pbm", ".pix", ".png", ".ppm", ".psd", ".ptx",
        ".raw", ".rgb", ".sgi", ".svg", ".tga", ".tif", ".tiff", ".webp", ".zzzimg"
    };
    return findFilesWithSuffixes(imageSuffixes);
}

std::vector<std::string> Library::getDocuments()
{
    std::vector<std::string> documentSuffixes = {
        ".doc", ".docx", ".md", ".odp", ".odt", ".pdf", ".ppt",
        ".pptx", ".rtf", ".rtfd", ".txt", ".zzzdoc"
    };
    return findFilesWithSuffixes(documentSuffixes);
}

std::vector<std::string> Library::getData()
{
    std::vector<std::string> dataSuffixes = {
        ".Z", ".bz2", ".csv", ".hdf5", ".json", ".mat", ".nc",
        ".ods", ".tar", ".tgz", ".vtk", ".xls", ".xml", ".xyz",
        ".zip", ".zzzdat"
    };
    return findFilesWithSuffixes(dataSuffixes);
}
