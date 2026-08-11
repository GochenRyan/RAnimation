#pragma once
#include <string>

#include <glm/glm.hpp>

class Tools
{
public:
    static std::string getFilenameExt(std::string filename);
    static std::string loadFileToString(std::string fileName);
};
