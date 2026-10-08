#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

// CPU-side mesh data produced by the USD loader and consumed by Model. Deliberately free of any RHI
// dependency so tools (TrsTextureBaker) can compile the loader without NRI.

namespace RAnimation
{
    struct RVertex
    {
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec4 color = glm::vec4(1.0f);
        glm::vec3 normal = glm::vec3(0.0f);
        glm::vec2 uv = glm::vec2(0.0f);
        glm::uvec4 boneNumber = glm::uvec4(0);
        glm::vec4 boneWeight = glm::vec4(0.0f);
    };

    // Material texture slots a mesh can reference. First-party replacement for assimp's aiTextureType
    // (the loader is now USD-based). Only Diffuse is currently consumed by the draw passes.
    enum class TextureType : uint8_t
    {
        Diffuse
    };

    struct RMesh
    {
        std::vector<RVertex> vertices{};
        std::vector<uint32_t> indices{};
        std::unordered_map<TextureType, std::string> textures{};
        bool usesPBRColors = false;
    };
} // namespace RAnimation
