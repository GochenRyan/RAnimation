// pimpl-style USD scene exporter: the only translation unit besides UsdModelLoader.cpp that includes pxr.
// Writes the current scene as a USD composition - one referencing prim per ModelInstance.

#include <Model/UsdSceneExporter.h>

#include "UsdPluginRegistration.h"
#include "UsdConversion.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <fmt/base.h>
#include <fmt/color.h>

#include <Model/Model.h>
#include <Model/ModelInstance.h>
#include <Model/AnimClip.h>

#include <pxr/pxr.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/references.h>
#include <pxr/usd/usdGeom/xform.h>
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
            if (!model)
            {
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

            // editor state that is not part of the xform itself
            const InstanceSettings settings = instance->GetInstanceSettings();
            // authoritative round-trip value; the clip name is written too for human readability
            prim.SetCustomDataByKey(TfToken("ranim:animClipNr"), VtValue(static_cast<int>(settings.mAnimClipNr)));

            const auto& clips = model->GetAnimClips();
            if (settings.mAnimClipNr < clips.size())
            {
                prim.SetCustomDataByKey(TfToken("ranim:animClip"),
                                        VtValue(clips.at(settings.mAnimClipNr)->GetClipName()));
            }

            ++index;
        }

        if (!stage->GetRootLayer()->Export(outPath))
        {
            fmt::print(stderr, fg(fmt::color::red), "UsdSceneExporter: failed to export '{}'\n", outPath);
            return false;
        }

        fmt::print("UsdSceneExporter: exported {} instance(s) to '{}'\n", index, outPath);
        return true;
    }

    bool ImportSceneFromUsd(const std::string& scenePath, std::vector<ImportedSceneInstance>& out,
                            CameraRig& cameraOut)
    {
        detail::RegisterUsdPluginsOnce();

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

        for (const UsdPrim& prim : stage->Traverse())
        {
            // an instance prim is any prim that references a source asset (as written by ExportSceneToUsd)
            SdfReferenceListOp refOp;
            if (!prim.GetMetadata(SdfFieldKeys->References, &refOp))
            {
                continue;
            }
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
            // resolve the (usually relative) reference path against the scene layer -> loadable path
            imported.assetPath = rootLayer->ComputeAbsolutePath(refs.front().GetAssetPath());

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
            const VtValue clipNr = prim.GetCustomDataByKey(TfToken("ranim:animClipNr"));
            if (clipNr.IsHolding<int>())
            {
                imported.settings.mAnimClipNr = static_cast<unsigned int>(std::max(0, clipNr.Get<int>()));
            }

            out.emplace_back(std::move(imported));
        }

        fmt::print("UsdSceneExporter: imported {} instance(s) from '{}'\n", out.size(), scenePath);
        return true;
    }
} // namespace RAnimation
