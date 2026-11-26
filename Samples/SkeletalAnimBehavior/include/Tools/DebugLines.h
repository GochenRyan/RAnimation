#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace RAnimation
{
    // CPU-side line list filled by gameplay / debug systems once per frame and drawn by GizmoDrawPass after
    // its own axis gizmo. Pure data; nothing here knows about the GPU.
    struct DebugLineVertex
    {
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 color = glm::vec3(1.0f);
    };

    struct DebugLineList
    {
        std::vector<DebugLineVertex> vertices;

        void Clear() { vertices.clear(); }
        size_t LineCount() const { return vertices.size() / 2; }

        void Line(const glm::vec3& a, const glm::vec3& b, const glm::vec3& color)
        {
            vertices.push_back({a, color});
            vertices.push_back({b, color});
        }

        // 12 edges of an axis-aligned box.
        void Box(const glm::vec3& mn, const glm::vec3& mx, const glm::vec3& color)
        {
            const glm::vec3 c[8] = {
                    {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z},
                    {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z},
            };
            static constexpr int e[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            for (const auto& edge : e)
            {
                Line(c[edge[0]], c[edge[1]], color);
            }
        }
    };
} // namespace RAnimation
