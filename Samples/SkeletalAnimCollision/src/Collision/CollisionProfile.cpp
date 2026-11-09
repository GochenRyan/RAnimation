#include <Collision/CollisionProfile.h>

#include <filesystem>
#include <fstream>
#include <iterator>

#include <fmt/format.h>

#include <pxr/base/js/json.h>

#include <Model/Model.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace RAnimation
{
    namespace
    {
        bool ReadNumber(const JsValue& v, float& out)
        {
            if (v.IsReal())
            {
                out = static_cast<float>(v.GetReal());
                return true;
            }
            if (v.IsInt())
            {
                out = static_cast<float>(v.GetInt());
                return true;
            }
            return false;
        }

        float NumberOr(const JsObject& obj, const char* key, float fallback)
        {
            auto it = obj.find(key);
            float v = fallback;
            if (it != obj.end())
            {
                ReadNumber(it->second, v);
            }
            return v;
        }

        std::string StringOr(const JsObject& obj, const char* key, const std::string& fallback)
        {
            auto it = obj.find(key);
            return it != obj.end() && it->second.IsString() ? it->second.GetString() : fallback;
        }

        // Reads a fixed-size numeric array; returns false when the key is present but malformed.
        bool ReadArray(const JsObject& obj, const char* key, float* out, size_t count, bool& present)
        {
            auto it = obj.find(key);
            present = it != obj.end();
            if (!present)
            {
                return true;
            }
            if (!it->second.IsArray())
            {
                return false;
            }
            const JsArray arr = it->second.GetJsArray();
            if (arr.size() != count)
            {
                return false;
            }
            for (size_t i = 0; i < count; ++i)
            {
                if (!ReadNumber(arr[i], out[i]))
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    const char* CollisionShapeTypeName(CollisionShapeDesc::Type type)
    {
        switch (type)
        {
            case CollisionShapeDesc::Type::Capsule: return "capsule";
            case CollisionShapeDesc::Type::Box: return "box";
            case CollisionShapeDesc::Type::Sphere: return "sphere";
        }
        return "?";
    }

    std::string CollisionProfile::PathForStem(const std::string& stem)
    {
        return (std::filesystem::path(ASSETS_SRC_DIR "/SkeletalAnimCollision/collision") / (stem + ".collision.json"))
                .generic_string();
    }

    std::string CollisionProfile::PathForModel(const Model& model)
    {
        // "<dir>/<Stem>.asset.usda" -> "<Stem>"
        std::filesystem::path p(model.GetModelFileNamePath());
        std::string stem = p.filename().generic_string();
        for (int guard = 0; guard < 3; ++guard)
        {
            const size_t dot = stem.rfind('.');
            if (dot == std::string::npos)
            {
                break;
            }
            stem = stem.substr(0, dot);
        }
        return PathForStem(stem);
    }

    bool CollisionProfile::Load(const std::string& path, CollisionProfile& out, std::string& outError)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open())
        {
            outError = fmt::format("could not open '{}'", path);
            return false;
        }
        const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!Parse(content, path, out, outError))
        {
            return false;
        }
        out.path = path;
        return true;
    }

    bool CollisionProfile::Parse(const std::string& json, const std::string& sourceName, CollisionProfile& out, std::string& outError)
    {
        JsParseError parseError;
        const JsValue root = JsParseString(json, &parseError);
        if (!root.IsObject())
        {
            outError = fmt::format("'{}' line {}: {}", sourceName, parseError.line, parseError.reason);
            return false;
        }

        out = CollisionProfile{};
        out.path = sourceName;
        const JsObject obj = root.GetJsObject();
        out.skeleton = StringOr(obj, "skeleton", "");
        out.margin = NumberOr(obj, "margin", 0.0f);

        if (auto it = obj.find("wholeBody"); it != obj.end() && it->second.IsObject())
        {
            const JsObject wb = it->second.GetJsObject();
            out.hasWholeBody = true;
            out.wholeBodyHalfHeight = NumberOr(wb, "halfHeight", 0.0f);
            out.wholeBodyRadius = NumberOr(wb, "radius", 0.0f);
        }

        auto shapesIt = obj.find("shapes");
        if (shapesIt == obj.end() || !shapesIt->second.IsArray())
        {
            outError = fmt::format("'{}' has no \"shapes\" array", sourceName);
            return false;
        }

        size_t index = 0;
        for (const JsValue& value : shapesIt->second.GetJsArray())
        {
            const size_t i = index++;
            if (!value.IsObject())
            {
                outError = fmt::format("'{}' shapes[{}] is not an object", sourceName, i);
                return false;
            }
            const JsObject s = value.GetJsObject();
            CollisionShapeDesc d;
            d.bone = StringOr(s, "bone", "");
            if (d.bone.empty())
            {
                outError = fmt::format("'{}' shapes[{}] has no \"bone\"", sourceName, i);
                return false;
            }
            const std::string type = StringOr(s, "type", "");
            if (type == "capsule")
            {
                d.type = CollisionShapeDesc::Type::Capsule;
            }
            else if (type == "box")
            {
                d.type = CollisionShapeDesc::Type::Box;
            }
            else if (type == "sphere")
            {
                d.type = CollisionShapeDesc::Type::Sphere;
            }
            else
            {
                outError = fmt::format("'{}' shapes[{}] ({}): unknown type '{}'", sourceName, i, d.bone, type);
                return false;
            }

            bool present = false;
            float v[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            if (!ReadArray(s, "center", v, 3, present))
            {
                outError = fmt::format("'{}' shapes[{}] ({}): \"center\" must be 3 numbers", sourceName, i, d.bone);
                return false;
            }
            if (present)
            {
                d.center = glm::vec3(v[0], v[1], v[2]);
            }
            v[0] = v[1] = v[2] = 0.0f;
            v[3] = 1.0f;
            if (!ReadArray(s, "rotation", v, 4, present))
            {
                outError = fmt::format("'{}' shapes[{}] ({}): \"rotation\" must be 4 numbers (x,y,z,w)", sourceName, i, d.bone);
                return false;
            }
            if (present)
            {
                d.rotation = glm::normalize(glm::quat(v[3], v[0], v[1], v[2]));
            }

            switch (d.type)
            {
                case CollisionShapeDesc::Type::Capsule:
                    d.halfHeight = NumberOr(s, "halfHeight", -1.0f);
                    d.radius = NumberOr(s, "radius", -1.0f);
                    if (d.halfHeight < 0.0f || d.radius <= 0.0f)
                    {
                        outError = fmt::format("'{}' shapes[{}] ({}): capsule needs halfHeight >= 0 and radius > 0", sourceName, i, d.bone);
                        return false;
                    }
                    break;
                case CollisionShapeDesc::Type::Sphere:
                    d.radius = NumberOr(s, "radius", -1.0f);
                    if (d.radius <= 0.0f)
                    {
                        outError = fmt::format("'{}' shapes[{}] ({}): sphere needs radius > 0", sourceName, i, d.bone);
                        return false;
                    }
                    break;
                case CollisionShapeDesc::Type::Box:
                    if (!ReadArray(s, "halfExtents", v, 3, present) || !present || v[0] <= 0.0f || v[1] <= 0.0f || v[2] <= 0.0f)
                    {
                        outError = fmt::format("'{}' shapes[{}] ({}): box needs 3 positive halfExtents", sourceName, i, d.bone);
                        return false;
                    }
                    d.halfExtents = glm::vec3(v[0], v[1], v[2]);
                    break;
            }

            if (auto m = s.find("merged"); m != s.end() && m->second.IsArray())
            {
                for (const JsValue& name : m->second.GetJsArray())
                {
                    if (name.IsString())
                    {
                        d.merged.push_back(name.GetString());
                    }
                }
            }
            out.shapes.push_back(std::move(d));
        }

        if (out.shapes.empty())
        {
            outError = fmt::format("'{}' has no shapes", sourceName);
            return false;
        }
        return true;
    }

    bool CollisionProfile::Resolve(const Model& model, std::string& outError)
    {
        std::string missing;
        for (CollisionShapeDesc& d : shapes)
        {
            d.nodeIndex = model.FindNodeIndex(d.bone);
            if (d.nodeIndex < 0)
            {
                missing += missing.empty() ? d.bone : ", " + d.bone;
            }
        }
        if (!missing.empty())
        {
            outError = fmt::format("'{}': bones not in '{}': {}", path, model.GetModelFileName(), missing);
            return false;
        }
        return true;
    }
} // namespace RAnimation
