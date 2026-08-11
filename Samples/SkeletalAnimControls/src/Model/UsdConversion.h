#pragma once
// Internal helper shared by UsdModelLoader.cpp and UsdSceneExporter.cpp (the only two TUs that include
// pxr). Single choke point for the canonical-space contract:
//
//     canonical engine space = Y-up, right-handed, metres, rightHanded triangle winding
//
// Import folds a stage into canonical space from STAGE METADATA ONLY (upAxis, metersPerUnit); authored
// prim transforms are always honored as-is via normal composition. That split is what prevents the
// Z-up correction from double-applying: an asset like animated_woman is authored Y-up with an explicit
// Armature rotateXYZ(-90,0,0) - it arrives through GetLocalToWorldTransform - while the synthetic
// Rx(-90) below is added only when the stage itself declares upAxis == Z. An asset that is BOTH Z-up
// and carries a baked -90 rotation is an authoring error this layer cannot detect.

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <pxr/pxr.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>

namespace RAnimation
{
    namespace detail
    {
        namespace usdconv
        {
            inline constexpr double kCanonicalMetersPerUnit = 1.0;
            // canonical up axis is UsdGeomTokens->y

            // GfMatrix4d is row-major / row-vector (translation in row 3); glm is column-major /
            // column-vector. The glm ctor takes column-major values, so feeding USD rows as glm columns
            // yields the correct (transposed) column-vector matrix - the standard USD<->glm conversion.
            inline glm::mat4 GlmFromGf(const PXR_NS::GfMatrix4d& m)
            {
                return glm::mat4(static_cast<float>(m[0][0]), static_cast<float>(m[0][1]),
                                 static_cast<float>(m[0][2]), static_cast<float>(m[0][3]),
                                 static_cast<float>(m[1][0]), static_cast<float>(m[1][1]),
                                 static_cast<float>(m[1][2]), static_cast<float>(m[1][3]),
                                 static_cast<float>(m[2][0]), static_cast<float>(m[2][1]),
                                 static_cast<float>(m[2][2]), static_cast<float>(m[2][3]),
                                 static_cast<float>(m[3][0]), static_cast<float>(m[3][1]),
                                 static_cast<float>(m[3][2]), static_cast<float>(m[3][3]));
            }

            // Stage space -> canonical engine space, from stage metadata only:
            //   Rx(-90 deg)          iff UsdGeomGetStageUpAxis(stage) == UsdGeomTokens->z
            //   * scale(mPU)         from UsdGeomGetStageMetersPerUnit (clamped: <= 0 -> 1.0)
            // Rotation and uniform scale commute; the order here is rotate * scale by definition.
            inline glm::mat4 StageToCanonicalMatrix(const PXR_NS::UsdStageRefPtr& stage)
            {
                double metersPerUnit = PXR_NS::UsdGeomGetStageMetersPerUnit(stage);
                if (metersPerUnit <= 0.0)
                {
                    metersPerUnit = 1.0;
                }

                glm::mat4 canonical = glm::scale(glm::mat4(1.0f), glm::vec3(static_cast<float>(metersPerUnit)));
                if (PXR_NS::UsdGeomGetStageUpAxis(stage) == PXR_NS::UsdGeomTokens->z)
                {
                    canonical = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)) *
                                canonical;
                }
                return canonical;
            }

            // True when the mesh winding matches canonical space (unauthored orientation falls back to
            // the USD default, rightHanded).
            inline bool IsMeshOrientationCanonical(const PXR_NS::UsdGeomMesh& mesh)
            {
                PXR_NS::TfToken orientation = PXR_NS::UsdGeomTokens->rightHanded;
                mesh.GetOrientationAttr().Get(&orientation);
                return orientation != PXR_NS::UsdGeomTokens->leftHanded;
            }

            // Declares canonical space on exporter-created stages so external DCCs interpret the file
            // correctly. Values written by the engine are already canonical - this is declaration, not
            // transformation.
            inline void AuthorCanonicalStageMetadata(const PXR_NS::UsdStageRefPtr& stage)
            {
                PXR_NS::UsdGeomSetStageUpAxis(stage, PXR_NS::UsdGeomTokens->y);
                PXR_NS::UsdGeomSetStageMetersPerUnit(stage, kCanonicalMetersPerUnit);
            }
        } // namespace usdconv
    } // namespace detail
} // namespace RAnimation
