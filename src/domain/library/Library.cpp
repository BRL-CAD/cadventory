// Library.cpp

#include "Library.h"
#include "ProcessGFiles.h"

#include <set>
#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <filesystem>

namespace fs = std::filesystem;

Library::Library(const char* _label, const char* _path)
    : shortName(_label ? _label : ""),
    fullPath(_path ? _path : ""),
    model(new Model(_path)),
    index(nullptr)
{
}

Library::~Library()
{
    delete index;
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
    index = new FilesystemIndexer(fullPath.c_str());
    return index->indexed();
}

void Library::loadDatabase()
{

}

std::vector<std::string> Library::getModels()
{
    /* Care about files with a .g extension */
    std::vector<std::string> modelSuffixes = {".g"};
    std::set<std::string> uniqueFiles; // Using set to avoid duplicates

    if (!index) {
        indexFiles();
    }

    auto files = index->findFilesWithSuffixes(modelSuffixes);
    for (const std::string& file : files) {
        // Make path relative to fullPath
        std::string relativePath = fs::relative(file, fullPath).string();
        uniqueFiles.insert(relativePath);
    }

    std::vector<std::string> filePaths(uniqueFiles.begin(), uniqueFiles.end());

    return filePaths;
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

    if (!index) {
        indexFiles();
    }

    return index->findFilesWithSuffixes(geometrySuffixes);
}

std::vector<std::string> Library::getImages()
{
    std::vector<std::string> imageSuffixes = {
        ".bmp", ".bw", ".cgm", ".dds", ".dpx", ".exr", ".gif", ".hdr",
        ".jpeg", ".jpg", ".pbm", ".pix", ".png", ".ppm", ".psd", ".ptx",
        ".raw", ".rgb", ".sgi", ".svg", ".tga", ".tif", ".tiff", ".webp", ".zzzimg"
    };

    if (!index) {
        indexFiles();
    }

    return index->findFilesWithSuffixes(imageSuffixes);
}

std::vector<std::string> Library::getDocuments()
{
    std::vector<std::string> documentSuffixes = {
        ".doc", ".docx", ".md", ".odp", ".odt", ".pdf", ".ppt",
        ".pptx", ".rtf", ".rtfd", ".txt", ".zzzdoc"
    };

    if (!index) {
        indexFiles();
    }

    return index->findFilesWithSuffixes(documentSuffixes);
}

std::vector<std::string> Library::getData()
{
    std::vector<std::string> dataSuffixes = {
        ".Z", ".bz2", ".csv", ".hdf5", ".json", ".mat", ".nc",
        ".ods", ".tar", ".tgz", ".vtk", ".xls", ".xml", ".xyz",
        ".zip", ".zzzdat"
    };

    if (!index) {
        indexFiles();
    }

    return index->findFilesWithSuffixes(dataSuffixes);
}
