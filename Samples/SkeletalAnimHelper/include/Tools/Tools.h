#pragma once
#include <string>

#include <assimp/matrix4x4.h>
#include <glm/glm.hpp>

class Tools
{
public:
    static std::string getFilenameExt(std::string filename);
    static std::string loadFileToString(std::string fileName);

    static glm::mat4 convertAiToGLM(aiMatrix4x4 inMat);
};
