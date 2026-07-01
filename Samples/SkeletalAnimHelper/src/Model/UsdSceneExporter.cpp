// pimpl-style USD scene exporter: the only translation unit besides UsdModelLoader.cpp that includes pxr.
// Writes the current scene as a USD composition - one referencing prim per ModelInstance.

#include <Model/UsdSceneExporter.h>

#include "UsdPluginRegistration.h"

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
} // namespace

namespace RAnimation
{
    bool ExportSceneToUsd(const ModelAndInstanceData& modInstData, const std::string& outPath)
    {
        detail::RegisterUsdPluginsOnce();

        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        if (!stage)
        {
            fmt::print(stderr, fg(fmt::color::red), "UsdSceneExporter: could not create stage\n");
            return false;
        }

        UsdGeomXform sceneRoot = UsdGeomXform::Define(stage, SdfPath("/Scene"));
        stage->SetDefaultPrim(sceneRoot.GetPrim());

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
            prim.SetCustomDataByKey(TfToken("ranim:swapYZAxis"), VtValue(instance->GetSwapYZAxis()));
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

    bool ImportSceneFromUsd(const std::string& scenePath, std::vector<ImportedSceneInstance>& out)
    {
        detail::RegisterUsdPluginsOnce();

        UsdStageRefPtr stage = UsdStage::Open(scenePath);
        if (!stage)
        {
            fmt::print(stderr, fg(fmt::color::red), "UsdSceneExporter: could not open scene '{}'\n", scenePath);
            return false;
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
            const VtValue swap = prim.GetCustomDataByKey(TfToken("ranim:swapYZAxis"));
            if (swap.IsHolding<bool>())
            {
                imported.settings.mSwapYZAxis = swap.Get<bool>();
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
