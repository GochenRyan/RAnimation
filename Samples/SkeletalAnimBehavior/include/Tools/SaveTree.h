#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

namespace RAnimation
{
    // USD-free tree of named scopes with typed values: the in-memory form of a checkpoint save. Systems
    // write / read their runtime into a SaveNode (no pxr), Behavior/BehaviorSaveIo.cpp maps the tree onto
    // prims (one Scope per node, one ranim:<key> attribute per value).
    using SaveValue = std::variant<bool, int, uint64_t, float, glm::vec3, std::string, std::vector<std::string>>;

    struct SaveNode
    {
        std::string name;
        std::vector<std::pair<std::string, SaveValue>> values;
        std::vector<SaveNode> children;

        void Set(const std::string& key, SaveValue value)
        {
            for (auto& [k, v] : values)
            {
                if (k == key)
                {
                    v = std::move(value);
                    return;
                }
            }
            values.emplace_back(key, std::move(value));
        }

        const SaveValue* Find(const std::string& key) const
        {
            for (const auto& [k, v] : values)
            {
                if (k == key)
                {
                    return &v;
                }
            }
            return nullptr;
        }

        // Typed read with the numeric promotions a USD round trip may introduce (int <-> uint64, int -> float).
        template <typename T> bool Get(const std::string& key, T& out) const
        {
            const SaveValue* v = Find(key);
            if (v == nullptr)
            {
                return false;
            }
            if (const T* exact = std::get_if<T>(v))
            {
                out = *exact;
                return true;
            }
            if constexpr (std::is_same_v<T, float>)
            {
                if (const int* i = std::get_if<int>(v))
                {
                    out = static_cast<float>(*i);
                    return true;
                }
            }
            if constexpr (std::is_same_v<T, int>)
            {
                if (const uint64_t* u = std::get_if<uint64_t>(v))
                {
                    out = static_cast<int>(*u);
                    return true;
                }
            }
            if constexpr (std::is_same_v<T, uint64_t>)
            {
                if (const int* i = std::get_if<int>(v))
                {
                    out = static_cast<uint64_t>(*i);
                    return true;
                }
            }
            return false;
        }

        template <typename T> T GetOr(const std::string& key, T fallback) const
        {
            T value = fallback;
            Get(key, value);
            return value;
        }

        SaveNode& Child(const std::string& childName)
        {
            for (SaveNode& c : children)
            {
                if (c.name == childName)
                {
                    return c;
                }
            }
            children.push_back(SaveNode{childName, {}, {}});
            return children.back();
        }

        const SaveNode* FindChild(const std::string& childName) const
        {
            for (const SaveNode& c : children)
            {
                if (c.name == childName)
                {
                    return &c;
                }
            }
            return nullptr;
        }
    };
} // namespace RAnimation
