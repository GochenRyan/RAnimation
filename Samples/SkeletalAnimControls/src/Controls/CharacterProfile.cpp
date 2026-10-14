#include <Controls/CharacterProfile.h>

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
        float NumberOr(const JsObject& obj, const char* key, float fallback)
        {
            auto it = obj.find(key);
            if (it == obj.end())
            {
                return fallback;
            }
            if (it->second.IsReal())
            {
                return static_cast<float>(it->second.GetReal());
            }
            if (it->second.IsInt())
            {
                return static_cast<float>(it->second.GetInt());
            }
            return fallback;
        }

        int IntOr(const JsObject& obj, const char* key, int fallback)
        {
            auto it = obj.find(key);
            if (it == obj.end())
            {
                return fallback;
            }
            if (it->second.IsInt())
            {
                return it->second.GetInt();
            }
            if (it->second.IsReal())
            {
                return static_cast<int>(it->second.GetReal());
            }
            return fallback;
        }

        bool BoolOr(const JsObject& obj, const char* key, bool fallback)
        {
            auto it = obj.find(key);
            return it != obj.end() && it->second.IsBool() ? it->second.GetBool() : fallback;
        }

        std::string StringOr(const JsObject& obj, const char* key, const std::string& fallback)
        {
            auto it = obj.find(key);
            return it != obj.end() && it->second.IsString() ? it->second.GetString() : fallback;
        }
    } // namespace

    std::string CharacterProfile::PathForModel(const Model& model)
    {
        // "<dir>/<Stem>.asset.usda" -> "<dir>/<Stem>.profile.json"
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
        return (p.parent_path() / (stem + ".profile.json")).generic_string();
    }

    bool CharacterProfile::Load(const std::string& path, CharacterProfile& out, std::string& outError)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open())
        {
            outError = fmt::format("could not open '{}'", path);
            return false;
        }
        const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        JsParseError parseError;
        const JsValue root = JsParseString(content, &parseError);
        if (!root.IsObject())
        {
            outError = fmt::format("'{}' line {}: {}", path, parseError.line, parseError.reason);
            return false;
        }

        out = CharacterProfile{};
        out.path = path;
        const JsObject obj = root.GetJsObject();
        const std::filesystem::path dir = std::filesystem::path(path).parent_path();

        const std::string graph = StringOr(obj, "graph", "");
        if (graph.empty())
        {
            outError = fmt::format("'{}' has no \"graph\" entry", path);
            return false;
        }
        out.graphPath = (dir / graph).lexically_normal().generic_string();

        if (auto it = obj.find("speeds"); it != obj.end() && it->second.IsObject())
        {
            const JsObject s = it->second.GetJsObject();
            out.walkSpeed = NumberOr(s, "walk", out.walkSpeed);
            out.jogSpeed = NumberOr(s, "jog", out.jogSpeed);
            out.sprintSpeed = NumberOr(s, "sprint", out.sprintSpeed);
            out.crouchSpeed = NumberOr(s, "crouch", out.crouchSpeed);
            out.jumpSpeed = NumberOr(s, "jump", out.jumpSpeed);
            out.gravity = NumberOr(s, "gravity", out.gravity);
            out.turnDegPerSec = NumberOr(s, "turnDegPerSec", out.turnDegPerSec);
        }
        if (auto it = obj.find("blend"); it != obj.end() && it->second.IsObject())
        {
            const JsObject b = it->second.GetJsObject();
            out.locoBlendSec = NumberOr(b, "loco", out.locoBlendSec);
            out.speedBlendRatePerSec = NumberOr(b, "speedRate", out.speedBlendRatePerSec);
            out.directionBlendRateDegPerSec = NumberOr(b, "directionRate", out.directionBlendRateDegPerSec);
            out.alphaBlendRatePerSec = NumberOr(b, "alphaRate", out.alphaBlendRatePerSec);
        }
        out.upperBodyMaskRoot = StringOr(obj, "upperBodyMaskRoot", "");

        if (auto it = obj.find("montages"); it != obj.end() && it->second.IsObject())
        {
            for (const auto& [name, value] : it->second.GetJsObject())
            {
                if (!value.IsObject())
                {
                    continue;
                }
                const JsObject m = value.GetJsObject();
                ProfileMontage pm;
                pm.slot = StringOr(m, "slot", "FullBody");
                pm.desc.name = name;
                pm.desc.clipName = StringOr(m, "clip", "");
                pm.desc.playRate = NumberOr(m, "playRate", 1.0f);
                pm.desc.blendInSec = NumberOr(m, "blendIn", 0.1f);
                pm.desc.blendOutSec = NumberOr(m, "blendOut", 0.2f);
                pm.desc.loop = BoolOr(m, "loop", false);
                pm.desc.priority = IntOr(m, "priority", 0);
                pm.desc.lockMovement = BoolOr(m, "lockMove", false);
                if (auto n = m.find("notifies"); n != m.end() && n->second.IsArray())
                {
                    for (const JsValue& nv : n->second.GetJsArray())
                    {
                        if (!nv.IsObject())
                        {
                            continue;
                        }
                        const JsObject no = nv.GetJsObject();
                        pm.desc.notifies.push_back({NumberOr(no, "time", 0.0f), StringOr(no, "name", "")});
                    }
                }
                if (pm.desc.clipName.empty())
                {
                    outError = fmt::format("'{}': montage '{}' has no clip", path, name);
                    return false;
                }
                out.montages[name] = std::move(pm);
            }
        }
        return true;
    }
} // namespace RAnimation
