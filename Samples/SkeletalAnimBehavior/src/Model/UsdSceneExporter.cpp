// pimpl-style USD scene exporter: the only translation unit besides UsdModelLoader.cpp that includes pxr.
// Writes the current scene as a USD composition - one referencing prim per ModelInstance.

#include <Model/UsdSceneExporter.h>

#include "UsdPluginRegistration.h"
#include "UsdConversion.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <fmt/base.h>
#include <fmt/color.h>
#include <fmt/format.h>
#include <glm/gtc/quaternion.hpp>

#include <Model/Model.h>
#include <Model/ModelInstance.h>
#include <Model/AnimClip.h>

#include <pxr/pxr.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/dictionary.h>
#include <pxr/base/vt/types.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/references.h>
#include <pxr/usd/usdGeom/boundable.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdGeom/xformOp.h>

PXR_NAMESPACE_USING_DIRECTIVE

using namespace RAnimation;

namespace
{
    // Reference path relative to the output layer when possible (portable), else absolute.
    std::string referencePathFor(const std::string& sourcePath, const std::string& outPath)
    {
        std::error_code ec;
        const std::filesystem::path absSource = std::filesystem::absolute(sourcePath, ec);
        const std::filesystem::path outDir = std::filesystem::path(outPath).parent_path();

        std::filesystem::path rel = std::filesystem::relative(absSource, outDir, ec);
        if (!ec && !rel.empty())
        {
            return rel.generic_string();
        }
        return absSource.generic_string();
    }

    // ---- Camera rig serialization: ranim:cam<i>:<field> customData on the /Scene root prim ----
    // Slot indices match CameraType: 0 Free, 1 FirstPerson, 2 ThirdPerson, 3 Stationary.
    std::string CamKey(int i, const char* field)
    {
        return "ranim:cam" + std::to_string(i) + ":" + field;
    }

    void SetF(const UsdPrim& p, int i, const char* f, float v) { p.SetCustomDataByKey(TfToken(CamKey(i, f)), VtValue(v)); }
    void SetI(const UsdPrim& p, int i, const char* f, int v) { p.SetCustomDataByKey(TfToken(CamKey(i, f)), VtValue(v)); }
    void SetStr(const UsdPrim& p, int i, const char* f, const std::string& v)
    {
        p.SetCustomDataByKey(TfToken(CamKey(i, f)), VtValue(v));
    }
    void SetVec3(const UsdPrim& p, int i, const char* f, const glm::vec3& v)
    {
        p.SetCustomDataByKey(TfToken(CamKey(i, f)), VtValue(GfVec3f(v.x, v.y, v.z)));
    }

    void GetF(const UsdPrim& p, int i, const char* f, float& v)
    {
        const VtValue x = p.GetCustomDataByKey(TfToken(CamKey(i, f)));
        if (x.IsHolding<float>())
        {
            v = x.Get<float>();
        }
    }
    void GetStr(const UsdPrim& p, int i, const char* f, std::string& v)
    {
        const VtValue x = p.GetCustomDataByKey(TfToken(CamKey(i, f)));
        if (x.IsHolding<std::string>())
        {
            v = x.Get<std::string>();
        }
    }
    void GetVec3(const UsdPrim& p, int i, const char* f, glm::vec3& v)
    {
        const VtValue x = p.GetCustomDataByKey(TfToken(CamKey(i, f)));
        if (x.IsHolding<GfVec3f>())
        {
            const GfVec3f g = x.Get<GfVec3f>();
            v = glm::vec3(g[0], g[1], g[2]);
        }
    }

    void WriteCommon(const UsdPrim& p, int i, const CameraCommon& c)
    {
        SetI(p, i, "projection", static_cast<int>(c.projection));
        SetF(p, i, "fov", c.fovDeg);
        SetF(p, i, "near", c.nearZ);
        SetF(p, i, "far", c.farZ);
        SetF(p, i, "orthoHalfHeight", c.orthoHalfHeight);
    }
    void ReadCommon(const UsdPrim& p, int i, CameraCommon& c)
    {
        const VtValue proj = p.GetCustomDataByKey(TfToken(CamKey(i, "projection")));
        if (proj.IsHolding<int>())
        {
            c.projection = static_cast<ProjectionType>(std::clamp(proj.Get<int>(), 0, 1));
        }
        GetF(p, i, "fov", c.fovDeg);
        GetF(p, i, "near", c.nearZ);
        GetF(p, i, "far", c.farZ);
        GetF(p, i, "orthoHalfHeight", c.orthoHalfHeight);
    }

    void WriteRig(const UsdPrim& p, const CameraRig& rig)
    {
        p.SetCustomDataByKey(TfToken("ranim:activeCamera"), VtValue(rig.active));

        WriteCommon(p, 0, rig.free.common);
        SetVec3(p, 0, "position", rig.free.position);
        SetF(p, 0, "yaw", rig.free.yawDeg);
        SetF(p, 0, "pitch", rig.free.pitchDeg);
        SetF(p, 0, "moveSpeed", rig.free.moveSpeed);
        SetF(p, 0, "mouseSensitivity", rig.free.mouseSensitivity);

        WriteCommon(p, 1, rig.firstPerson.common);
        SetStr(p, 1, "headBoneName", rig.firstPerson.headBoneName);
        SetVec3(p, 1, "eyeOffset", rig.firstPerson.eyeOffset);
        SetF(p, 1, "forwardPush", rig.firstPerson.forwardPush);
        SetF(p, 1, "yaw", rig.firstPerson.yawDeg);
        SetF(p, 1, "pitch", rig.firstPerson.pitchDeg);
        SetF(p, 1, "roll", rig.firstPerson.rollDeg);
        SetF(p, 1, "mouseSensitivity", rig.firstPerson.mouseSensitivity);

        WriteCommon(p, 2, rig.thirdPerson.common);
        SetStr(p, 2, "headBoneName", rig.thirdPerson.headBoneName);
        SetF(p, 2, "distance", rig.thirdPerson.distance);
        SetF(p, 2, "pitch", rig.thirdPerson.pitchDeg);
        SetF(p, 2, "yaw", rig.thirdPerson.yawDeg);
        SetF(p, 2, "damping", rig.thirdPerson.damping);
        SetF(p, 2, "mouseSensitivity", rig.thirdPerson.mouseSensitivity);
        SetF(p, 2, "scrollSpeed", rig.thirdPerson.scrollSpeed);

        WriteCommon(p, 3, rig.stationary.common);
        SetVec3(p, 3, "position", rig.stationary.position);
        SetF(p, 3, "damping", rig.stationary.damping);
    }

    void ReadRig(const UsdPrim& p, CameraRig& rig)
    {
        const VtValue active = p.GetCustomDataByKey(TfToken("ranim:activeCamera"));
        if (active.IsHolding<int>())
        {
            rig.active = std::clamp(active.Get<int>(), 0, 3);
        }

        ReadCommon(p, 0, rig.free.common);
        GetVec3(p, 0, "position", rig.free.position);
        GetF(p, 0, "yaw", rig.free.yawDeg);
        GetF(p, 0, "pitch", rig.free.pitchDeg);
        GetF(p, 0, "moveSpeed", rig.free.moveSpeed);
        GetF(p, 0, "mouseSensitivity", rig.free.mouseSensitivity);

        ReadCommon(p, 1, rig.firstPerson.common);
        GetStr(p, 1, "headBoneName", rig.firstPerson.headBoneName);
        GetVec3(p, 1, "eyeOffset", rig.firstPerson.eyeOffset);
        GetF(p, 1, "forwardPush", rig.firstPerson.forwardPush);
        GetF(p, 1, "yaw", rig.firstPerson.yawDeg);
        GetF(p, 1, "pitch", rig.firstPerson.pitchDeg);
        GetF(p, 1, "roll", rig.firstPerson.rollDeg);
        GetF(p, 1, "mouseSensitivity", rig.firstPerson.mouseSensitivity);

        ReadCommon(p, 2, rig.thirdPerson.common);
        GetStr(p, 2, "headBoneName", rig.thirdPerson.headBoneName);
        GetF(p, 2, "distance", rig.thirdPerson.distance);
        GetF(p, 2, "pitch", rig.thirdPerson.pitchDeg);
        GetF(p, 2, "yaw", rig.thirdPerson.yawDeg);
        GetF(p, 2, "damping", rig.thirdPerson.damping);
        GetF(p, 2, "mouseSensitivity", rig.thirdPerson.mouseSensitivity);
        GetF(p, 2, "scrollSpeed", rig.thirdPerson.scrollSpeed);

        ReadCommon(p, 3, rig.stationary.common);
        GetVec3(p, 3, "position", rig.stationary.position);
        GetF(p, 3, "damping", rig.stationary.damping);
    }

    // ---- entity id / agent config: customData ranim:entityId, ranim:agent:<field> on instance prims ----

    void WriteAgent(const UsdPrim& p, const AgentConfig& a)
    {
        const auto set = [&p](const char* field, const VtValue& v) { p.SetCustomDataByKey(TfToken(std::string("ranim:agent:") + field), v); };
        set("tree", VtValue(a.tree));
        set("tickHz", VtValue(a.tickHz));
        set("proximityRadius", VtValue(a.proximityRadius));
        set("fovHalfDeg", VtValue(a.fovHalfDeg));
        set("sightRange", VtValue(a.sightRange));
        set("possessed", VtValue(a.possessed));
    }

    bool NumberFrom(const VtValue& v, double& out)
    {
        if (v.IsHolding<float>())
            out = v.Get<float>();
        else if (v.IsHolding<double>())
            out = v.Get<double>();
        else if (v.IsHolding<int>())
            out = v.Get<int>();
        else if (v.IsHolding<int64_t>())
            out = static_cast<double>(v.Get<int64_t>());
        else if (v.IsHolding<unsigned int>())
            out = v.Get<unsigned int>();
        else if (v.IsHolding<uint64_t>())
            out = static_cast<double>(v.Get<uint64_t>());
        else if (v.IsHolding<bool>())
            out = v.Get<bool>() ? 1.0 : 0.0;
        else
            return false;
        return true;
    }

    EntityId ReadEntityId(const VtValue& v)
    {
        if (v.IsHolding<uint64_t>())
            return v.Get<uint64_t>();
        if (v.IsHolding<int64_t>())
            return static_cast<EntityId>(std::max<int64_t>(0, v.Get<int64_t>()));
        if (v.IsHolding<unsigned int>())
            return v.Get<unsigned int>();
        if (v.IsHolding<int>())
            return static_cast<EntityId>(std::max(0, v.Get<int>()));
        return kInvalidEntityId;
    }

    std::optional<AgentConfig> ReadAgent(const UsdPrim& p)
    {
        const VtValue v = p.GetCustomDataByKey(TfToken("ranim:agent"));
        if (!v.IsHolding<VtDictionary>())
        {
            return std::nullopt;
        }
        const VtDictionary& d = v.Get<VtDictionary>();
        AgentConfig a;
        const auto number = [&d](const char* key, float& field)
        {
            const auto it = d.find(key);
            double x = 0.0;
            if (it != d.end() && NumberFrom(it->second, x))
            {
                field = static_cast<float>(x);
            }
        };
        if (const auto it = d.find("tree"); it != d.end() && it->second.IsHolding<std::string>())
        {
            a.tree = it->second.Get<std::string>();
        }
        number("tickHz", a.tickHz);
        number("proximityRadius", a.proximityRadius);
        number("fovHalfDeg", a.fovHalfDeg);
        number("sightRange", a.sightRange);
        if (const auto it = d.find("possessed"); it != d.end())
        {
            double x = 0.0;
            a.possessed = NumberFrom(it->second, x) && x != 0.0;
        }
        return a;
    }

    // ---- static layer attributes (authored by the DCC, see Tools/Maya/BehaviorScene) ----

    bool StartsWith(const std::string& s, const char* prefix)
    {
        return s.rfind(prefix, 0) == 0;
    }

    std::string ReadStringAttr(const UsdPrim& p, const char* name)
    {
        const UsdAttribute attr = p.GetAttribute(TfToken(name));
        VtValue v;
        if (!attr || !attr.Get(&v))
        {
            return {};
        }
        if (v.IsHolding<std::string>())
            return v.Get<std::string>();
        if (v.IsHolding<TfToken>())
            return v.Get<TfToken>().GetString();
        return {};
    }

    bool ReadFloatAttr(const UsdPrim& p, const char* name, float& out)
    {
        const UsdAttribute attr = p.GetAttribute(TfToken(name));
        VtValue v;
        double x = 0.0;
        if (!attr || !attr.Get(&v) || !NumberFrom(v, x))
        {
            return false;
        }
        out = static_cast<float>(x);
        return true;
    }

    bool ReadIntAttr(const UsdPrim& p, const char* name, int& out)
    {
        float x = 0.0f;
        if (!ReadFloatAttr(p, name, x))
        {
            return false;
        }
        out = static_cast<int>(std::lround(x));
        return true;
    }

    // Oriented box of a boundable prim: its local extent pushed through world (= stage-to-canonical *
    // local-to-world). The scale of world goes into the half extents, so a cube exported in centimetres
    // under a 0.01 root scale comes out in metres.
    bool BoxFromPrim(const UsdPrim& prim, const glm::mat4& world, StaticBox& box)
    {
        const UsdGeomBoundable boundable(prim);
        if (!boundable)
        {
            return false;
        }
        VtVec3fArray extent;
        if (!boundable.GetExtentAttr().Get(&extent) || extent.size() != 2)
        {
            extent.clear();
            if (!UsdGeomBoundable::ComputeExtentFromPlugins(boundable, UsdTimeCode::Default(), &extent) || extent.size() != 2)
            {
                return false;
            }
        }
        const glm::vec3 mn(extent[0][0], extent[0][1], extent[0][2]);
        const glm::vec3 mx(extent[1][0], extent[1][1], extent[1][2]);
        const glm::vec3 localCenter = (mn + mx) * 0.5f;
        const glm::vec3 localHalf = (mx - mn) * 0.5f;

        box.center = glm::vec3(world * glm::vec4(localCenter, 1.0f));
        glm::vec3 axes[3];
        for (int i = 0; i < 3; ++i)
        {
            const glm::vec3 a(world[i]);
            const float s = glm::length(a);
            glm::vec3 unit(0.0f);
            unit[i] = 1.0f;
            axes[i] = s > 1e-8f ? a / s : unit;
            box.halfExtents[i] = localHalf[i] * s;
        }
        if (glm::dot(glm::cross(axes[0], axes[1]), axes[2]) < 0.0f)
        {
            axes[2] = -axes[2]; // mirrored transform: the box is symmetric, keep a proper rotation
        }
        box.rotation = glm::normalize(glm::quat_cast(glm::mat3(axes[0], axes[1], axes[2])));
        return true;
    }
} // namespace

namespace RAnimation
{
    bool ExportSceneToUsd(const ModelAndInstanceData& modInstData, const CameraRig& cameraRig,
                          const std::string& outPath)
    {
        detail::RegisterUsdPluginsOnce();

        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        if (!stage)
        {
            fmt::print(stderr, fg(fmt::color::red), "UsdSceneExporter: could not create stage\n");
            return false;
        }

        // Instance transforms below are already in canonical engine space (Y-up, metres) - declare it
        // so external DCCs interpret the file correctly.
        detail::usdconv::AuthorCanonicalStageMetadata(stage);

        UsdGeomXform sceneRoot = UsdGeomXform::Define(stage, SdfPath("/Scene"));
        stage->SetDefaultPrim(sceneRoot.GetPrim());

        // Camera rig (all four slots + active index) as customData on the scene root.
        WriteRig(sceneRoot.GetPrim(), cameraRig);

        int index = 0;
        for (const auto& instance : modInstData.miModelInstances)
        {
            if (!instance)
            {
                continue;
            }
            const std::shared_ptr<Model> model = instance->GetModel();
            if (!model || modInstData.IsStaticLayerModel(model.get()))
            {
                // static-layer display models come back through the subLayers below
                continue;
            }

            const SdfPath primPath("/Scene/Instance_" + std::to_string(index));
            UsdGeomXform xform = UsdGeomXform::Define(stage, primPath);
            UsdPrim prim = xform.GetPrim();

            // reference the source asset USD this instance was loaded from
            const std::string reference = referencePathFor(model->GetModelFileNamePath(), outPath);
            prim.GetReferences().AddReference(reference);

            // per-instance transform (rotation is stored in degrees, matching AddRotateXYZOp)
            const glm::vec3 t = instance->GetTranslation();
            const glm::vec3 r = instance->GetRotation();
            const float s = instance->GetScale();
            xform.AddTranslateOp().Set(GfVec3d(t.x, t.y, t.z));
            xform.AddRotateXYZOp().Set(GfVec3f(r.x, r.y, r.z));
            xform.AddScaleOp().Set(GfVec3f(s, s, s));

            // identity and agent configuration (nested dictionaries ranim = { entityId, agent = {...} })
            prim.SetCustomDataByKey(TfToken("ranim:entityId"), VtValue(static_cast<uint64_t>(instance->GetEntityId())));
            if (const std::optional<AgentConfig>& agent = instance->GetAgentConfig())
            {
                WriteAgent(prim, *agent);
            }

            // editor state that is not part of the xform itself
            const InstanceSettings settings = instance->GetInstanceSettings();
            // authoritative round-trip value; the clip name is written too for human readability
            prim.SetCustomDataByKey(TfToken("ranim:animClipNr"), VtValue(static_cast<int>(settings.mAnimClipNr)));
            prim.SetCustomDataByKey(TfToken("ranim:animPhase"), VtValue(settings.mAnimPhase));

            const auto& clips = model->GetAnimClips();
            if (settings.mAnimClipNr < clips.size())
            {
                prim.SetCustomDataByKey(TfToken("ranim:animClip"),
                                        VtValue(clips.at(settings.mAnimClipNr)->GetClipName()));
            }

            ++index;
        }

        // Static layers go in as relative subLayers. They are added to a detached copy of the layer: on the
        // in-memory stage a relative sublayer would be resolved against the anonymous layer and fail.
        SdfLayerRefPtr outLayer = SdfLayer::CreateAnonymous(".usda");
        outLayer->TransferContent(stage->GetRootLayer());
        std::vector<std::string> subLayers;
        for (const StaticLayer& layer : modInstData.miStaticLayers)
        {
            subLayers.push_back(referencePathFor(layer.path, outPath));
        }
        outLayer->SetSubLayerPaths(subLayers);

        if (!outLayer->Export(outPath))
        {
            fmt::print(stderr, fg(fmt::color::red), "UsdSceneExporter: failed to export '{}'\n", outPath);
            return false;
        }

        fmt::print("UsdSceneExporter: exported {} instance(s) and {} static layer(s) to '{}'\n", index, subLayers.size(), outPath);
        return true;
    }

    bool ImportSceneFromUsd(const std::string& scenePath, ImportedScene& scene)
    {
        detail::RegisterUsdPluginsOnce();
        std::vector<ImportedSceneInstance>& out = scene.instances;
        CameraRig& cameraOut = scene.camera;

        UsdStageRefPtr stage = UsdStage::Open(scenePath);
        if (!stage)
        {
            fmt::print(stderr, fg(fmt::color::red), "UsdSceneExporter: could not open scene '{}'\n", scenePath);
            return false;
        }

        // Camera rig from the /Scene root (default prim). Missing keys leave a slot at its default.
        UsdPrim scenePrim = stage->GetDefaultPrim();
        if (!scenePrim)
        {
            scenePrim = stage->GetPrimAtPath(SdfPath("/Scene"));
        }
        if (scenePrim)
        {
            ReadRig(scenePrim, cameraOut);
        }

        // Instance transforms are applied verbatim, so a non-canonical scene would silently misplace
        // instances. Warn only when the metadata is actually authored - legacy engine exports carry none.
        if (stage->HasAuthoredMetadata(UsdGeomTokens->upAxis) &&
            UsdGeomGetStageUpAxis(stage) != UsdGeomTokens->y)
        {
            fmt::print(stderr, fg(fmt::color::yellow),
                       "UsdSceneExporter: scene '{}' is not Y-up; instance transforms are applied verbatim\n",
                       scenePath);
        }
        if (stage->HasAuthoredMetadata(UsdGeomTokens->metersPerUnit) &&
            UsdGeomGetStageMetersPerUnit(stage) != detail::usdconv::kCanonicalMetersPerUnit)
        {
            fmt::print(stderr, fg(fmt::color::yellow),
                       "UsdSceneExporter: scene '{}' has metersPerUnit != 1; instance transforms are applied "
                       "verbatim\n",
                       scenePath);
        }

        const SdfLayerHandle rootLayer = stage->GetRootLayer();

        UsdPrimRange range = stage->Traverse();
        for (auto it = range.begin(); it != range.end(); ++it)
        {
            const UsdPrim& prim = *it;
            // an instance prim is any prim that references a source asset (as written by ExportSceneToUsd)
            SdfReferenceListOp refOp;
            if (!prim.GetMetadata(SdfFieldKeys->References, &refOp))
            {
                continue;
            }
            // the referenced asset's own prims are not instances
            it.PruneChildren();
            // AddReference prepends by default; fall back through the other list-op slots to be safe.
            SdfReferenceVector refs = refOp.GetPrependedItems();
            if (refs.empty())
            {
                refs = refOp.GetAppendedItems();
            }
            if (refs.empty())
            {
                refs = refOp.GetAddedItems();
            }
            if (refs.empty())
            {
                refs = refOp.GetExplicitItems();
            }
            if (refs.empty() || refs.front().GetAssetPath().empty())
            {
                continue;
            }

            ImportedSceneInstance imported;
            imported.primPath = prim.GetPath().GetString();
            // resolve the (usually relative) reference path against the scene layer -> loadable path
            imported.assetPath = rootLayer->ComputeAbsolutePath(refs.front().GetAssetPath());
            imported.entityId = ReadEntityId(prim.GetCustomDataByKey(TfToken("ranim:entityId")));
            imported.agent = ReadAgent(prim);

            // transform from the xformOps
            UsdGeomXformable xformable(prim);
            bool resetsXformStack = false;
            for (const UsdGeomXformOp& op : xformable.GetOrderedXformOps(&resetsXformStack))
            {
                switch (op.GetOpType())
                {
                    case UsdGeomXformOp::TypeTranslate:
                    {
                        GfVec3d v(0.0);
                        op.Get(&v);
                        imported.settings.mWorldPosition =
                                glm::vec3(static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]));
                        break;
                    }
                    case UsdGeomXformOp::TypeRotateXYZ:
                    {
                        GfVec3f v(0.0f);
                        op.Get(&v);
                        imported.settings.mWorldRotation = glm::vec3(v[0], v[1], v[2]);
                        break;
                    }
                    case UsdGeomXformOp::TypeScale:
                    {
                        GfVec3f v(1.0f);
                        op.Get(&v);
                        imported.settings.mScale = v[0];
                        break;
                    }
                    default:
                        break;
                }
            }

            // editor-only state stored as customData
            // ranim:swapYZAxis was retired with the canonical-space contract; a legacy 'true' meant the
            // scene relied on the removed per-instance axis flip, so tell the user to re-export.
            const VtValue swap = prim.GetCustomDataByKey(TfToken("ranim:swapYZAxis"));
            if (swap.IsHolding<bool>() && swap.Get<bool>())
            {
                fmt::print(stderr, fg(fmt::color::yellow),
                           "UsdSceneExporter: '{}' authors legacy ranim:swapYZAxis=1 - ignored; re-export the "
                           "scene\n",
                           prim.GetPath().GetString());
            }
            const VtValue phase = prim.GetCustomDataByKey(TfToken("ranim:animPhase"));
            if (phase.IsHolding<float>())
            {
                imported.settings.mAnimPhase = phase.Get<float>();
            }
            else if (phase.IsHolding<double>())
            {
                imported.settings.mAnimPhase = static_cast<float>(phase.Get<double>());
            }
            const VtValue clipNr = prim.GetCustomDataByKey(TfToken("ranim:animClipNr"));
            if (clipNr.IsHolding<int>())
            {
                imported.settings.mAnimClipNr = static_cast<unsigned int>(std::max(0, clipNr.Get<int>()));
            }

            out.emplace_back(std::move(imported));
        }

        // Static layers: every subLayer of the level file (authored order).
        for (const std::string& subLayer : rootLayer->GetSubLayerPaths())
        {
            ImportedStaticLayer layer;
            layer.path = rootLayer->ComputeAbsolutePath(subLayer);
            std::string error;
            if (!LoadLevelStatics(layer.path, layer.statics, error))
            {
                fmt::print(stderr, fg(fmt::color::red), "UsdSceneExporter: static layer '{}' skipped: {}\n", subLayer, error);
                continue;
            }
            scene.staticLayers.emplace_back(std::move(layer));
        }

        fmt::print("UsdSceneExporter: imported {} instance(s) and {} static layer(s) from '{}'\n",
                   out.size(),
                   scene.staticLayers.size(),
                   scenePath);
        return true;
    }

    bool LoadLevelStatics(const std::string& staticLayerPath, LevelStatics& out, std::string& error)
    {
        detail::RegisterUsdPluginsOnce();

        UsdStageRefPtr stage = UsdStage::Open(staticLayerPath);
        if (!stage)
        {
            error = "could not open '" + staticLayerPath + "'";
            return false;
        }

        const glm::mat4 stageToCanonical = detail::usdconv::StageToCanonicalMatrix(stage);
        UsdGeomXformCache xformCache(UsdTimeCode::Default());
        const auto warn = [&out](std::string message)
        {
            fmt::print(stderr, fg(fmt::color::yellow), "LevelStatics: {}\n", message);
            out.warnings.push_back(std::move(message));
        };

        for (const UsdPrim& prim : stage->Traverse())
        {
            const std::string name = prim.GetName().GetString();
            const std::string path = prim.GetPath().GetString();
            const std::string collider = ReadStringAttr(prim, "ranim:collider");
            const std::string marker = ReadStringAttr(prim, "ranim:marker");
            const glm::mat4 world = stageToCanonical * detail::usdconv::GlmFromGf(xformCache.GetLocalToWorldTransform(prim));

            // the naming prefix is the authoring convention; the ranim:* attribute is what counts
            if ((StartsWith(name, "COL_") || StartsWith(name, "BND_")) && collider.empty())
            {
                warn(fmt::format("'{}' is named like a collider but has no ranim:collider (run tag_static_props)", path));
            }
            if ((StartsWith(name, "ZONE_") || StartsWith(name, "WP_") || StartsWith(name, "SPAWN_")) && marker.empty())
            {
                warn(fmt::format("'{}' is named like a marker but has no ranim:marker (run tag_static_props)", path));
            }

            if (!collider.empty())
            {
                if (collider != "box")
                {
                    warn(fmt::format("'{}': ranim:collider = \"{}\" is not supported (only \"box\")", path, collider));
                    continue;
                }
                StaticCollider c;
                c.name = name;
                c.path = path;
                const std::string tag = ReadStringAttr(prim, "ranim:collisionTag");
                if (tag == "boundary" || (tag.empty() && StartsWith(name, "BND_")))
                {
                    c.tag = StaticColliderTag::Boundary;
                }
                else if (!tag.empty() && tag != "obstacle")
                {
                    warn(fmt::format("'{}': unknown ranim:collisionTag \"{}\", using obstacle", path, tag));
                }
                if (!BoxFromPrim(prim, world, c.box))
                {
                    warn(fmt::format("'{}': collider has no extent, skipped", path));
                    continue;
                }
                out.colliders.push_back(std::move(c));
            }

            if (marker == "zone")
            {
                StaticZone z;
                z.name = StartsWith(name, "ZONE_") ? name.substr(5) : name;
                z.path = path;
                if (!BoxFromPrim(prim, world, z.box))
                {
                    warn(fmt::format("'{}': zone has no extent, skipped", path));
                    continue;
                }
                out.zones.push_back(std::move(z));
            }
            else if (marker == "waypoint" || marker == "spawn")
            {
                StaticMarker m;
                m.name = name;
                m.path = path;
                m.kind = marker == "spawn" ? StaticMarkerKind::Spawn : StaticMarkerKind::Waypoint;
                m.position = glm::vec3(world[3]);
                const glm::vec3 forward = glm::mat3(world) * glm::vec3(0.0f, 0.0f, -1.0f);
                const glm::vec3 flat(forward.x, 0.0f, forward.z);
                m.forward = glm::length(flat) > 1e-6f ? glm::normalize(flat) : glm::vec3(0.0f, 0.0f, -1.0f);
                ReadFloatAttr(prim, "ranim:radius", m.radius);
                if (m.kind == StaticMarkerKind::Waypoint)
                {
                    m.route = ReadStringAttr(prim, "ranim:route");
                    int index = -1;
                    const bool hasIndex = ReadIntAttr(prim, "ranim:index", index);
                    // WP_<route>_<index>: route may itself contain underscores, the index is the last field
                    const size_t lastUnderscore = name.find_last_of('_');
                    if (StartsWith(name, "WP_") && lastUnderscore != std::string::npos && lastUnderscore > 3)
                    {
                        if (m.route.empty())
                        {
                            m.route = name.substr(3, lastUnderscore - 3);
                        }
                        if (!hasIndex)
                        {
                            const std::string digits = name.substr(lastUnderscore + 1);
                            if (!digits.empty() && std::all_of(digits.begin(), digits.end(), [](char ch) { return ch >= '0' && ch <= '9'; }))
                            {
                                index = std::stoi(digits);
                            }
                        }
                    }
                    m.index = index;
                    if (m.route.empty() || m.index < 0)
                    {
                        warn(fmt::format("'{}': waypoint without route / index (name it WP_<route>_<index>)", path));
                    }
                }
                out.markers.push_back(std::move(m));
            }
            else if (!marker.empty())
            {
                warn(fmt::format("'{}': unknown ranim:marker \"{}\"", path, marker));
            }
        }

        // duplicate waypoint indices make a route ambiguous
        for (size_t i = 0; i < out.markers.size(); ++i)
        {
            for (size_t j = i + 1; j < out.markers.size(); ++j)
            {
                const StaticMarker& a = out.markers[i];
                const StaticMarker& b = out.markers[j];
                if (a.kind == StaticMarkerKind::Waypoint && b.kind == StaticMarkerKind::Waypoint && !a.route.empty() &&
                    a.route == b.route && a.index == b.index)
                {
                    warn(fmt::format("waypoints '{}' and '{}' share route '{}' index {}", a.path, b.path, a.route, a.index));
                }
            }
        }

        fmt::print("LevelStatics: '{}' - {} collider(s), {} zone(s), {} marker(s), {} warning(s)\n",
                   staticLayerPath,
                   out.colliders.size(),
                   out.zones.size(),
                   out.markers.size(),
                   out.warnings.size());
        return true;
    }
} // namespace RAnimation
