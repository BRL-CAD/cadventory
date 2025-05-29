#ifndef MODELPARSER_H
#define MODELPARSER_H

#include <string>
#include <vector>
#include "ModelMetadata.h" 

/**
 * @brief Parses BRL-CAD model files to extract metadata
 *
 * Extracts title, object names, and structural information from
 * BRL-CAD .g files to provide context for the AI tagging system.
 */
class ModelParser {
public:
    ModelParser();

    // parse the model file and return the metadata
    /**
	* @brief Parses a BRL-CAD model file to extract metadata
    * 
	* @return ModelMetadata object containing the parsed information
	* @see ModelMetadata.cpp/.h
    */
    ModelMetadata parseModel(std::string filepath);
};

#endif // MODELPARSER_H