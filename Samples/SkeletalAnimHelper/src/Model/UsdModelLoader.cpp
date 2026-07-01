// The ONLY translation unit that includes OpenUSD (pxr) headers. Everything USD/boost/TBB stays here so
// it never pollutes the rest of the sample. The loader translates a UsdSkel asset into the plain CPU data
// declared in UsdModelLoader.h; Model owns the GPU upload and the runtime Node/Bone/AnimClip objects.

#include <Model/UsdModelLoader.h>

#include "UsdPluginRegistration.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/base.h>
#include <fmt/color.h>
#include <glm/gtc/matrix_transform.hpp>

#include <pxr/pxr.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/js/json.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/quatf.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3h.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/dictionary.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/primvar.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdSkel/skeleton.h>
#include <pxr/usd/usdSkel/bindingAPI.h>
#include <pxr/usd/usdSkel/animation.h>

PXR_NAMESPACE_USING_DIRECTIVE

using namespace RAnimation;

namespace
{
    // ----- conversions --------------------------------------------------------------------------------

    // GfMatrix4d is row-major / row-vector (translation in row 3); glm is column-major / column-vector.
    // The glm ctor takes column-major values, so feeding USD rows as glm columns yields the correct
    // (transposed) column-vector matrix - the standard USD<->glm conversion.
    glm::mat4 glmFromGf(const GfMatrix4d& m)
    {
        return glm::mat4(static_cast<float>(m[0][0]), static_cast<float>(m[0][1]), static_cast<float>(m[0][2]),
                         static_cast<float>(m[0][3]), static_cast<float>(m[1][0]), static_cast<float>(m[1][1]),
                         static_cast<float>(m[1][2]), static_cast<float>(m[1][3]), static_cast<float>(m[2][0]),
                         static_cast<float>(m[2][1]), static_cast<float>(m[2][2]), static_cast<float>(m[2][3]),
                         static_cast<float>(m[3][0]), static_cast<float>(m[3][1]), static_cast<float>(m[3][2]),
                         static_cast<float>(m[3][3]));
    }

    std::string jointLeaf(const std::string& jointPath)
    {
        const size_t slash = jointPath.find_last_of('/');
        return slash == std::string::npos ? jointPath : jointPath.substr(slash + 1);
    }

    std::string jointParentPath(const std::string& jointPath)
    {
        const size_t slash = jointPath.find_last_of('/');
        return slash == std::string::npos ? std::string() : jointPath.substr(0, slash);
    }

    // Interpolation of a USD attribute/primvar, used to pick which index feeds a face corner.
    enum class Interp
    {
        Constant,
        Uniform,
        Vertex,
        FaceVarying
    };

    Interp toInterp(const TfToken& token)
    {
        if (token == UsdGeomTokens->uniform)
        {
            return Interp::Uniform;
        }
        if (token == UsdGeomTokens->faceVarying)
        {
            return Interp::FaceVarying;
        }
        if (token == UsdGeomTokens->constant)
        {
            return Interp::Constant;
        }
        return Interp::Vertex; // vertex / varying
    }

    // Resolves the element index to sample for a given interpolation at a face corner.
    int sampleIndex(Interp interp, int faceIndex, int cornerIndex, int pointIndex)
    {
        switch (interp)
        {
            case Interp::Constant:
                return 0;
            case Interp::Uniform:
                return faceIndex;
            case Interp::FaceVarying:
                return cornerIndex;
            case Interp::Vertex:
            default:
                return pointIndex;
        }
    }

    // ----- skeleton -----------------------------------------------------------------------------------

    // Reads the Skeleton, builds the flattened node list (parent-before-child) and the bone list. Returns
    // maps from full joint-path token to the chosen node name and to the bone id, reused for skinning/anim.
    //
    // A synthetic root node carrying the skeleton prim's local-to-world transform is prepended so skinned
    // bones inherit the SkelRoot/Armature orientation (e.g. the Z-up -> Y-up conversion). It owns no bone,
    // so node indices are shifted by +1 relative to bone ids (which stay equal to the joint index).
    bool buildSkeleton(const UsdSkelSkeleton& skel,
                       const glm::mat4& skeletonLocalToWorld,
                       const std::string& syntheticRootName,
                       UsdLoadedModel& out,
                       std::unordered_map<std::string, std::string>& jointToName,
                       std::unordered_map<std::string, int>& jointToBoneId)
    {
        VtTokenArray joints;
        if (!skel.GetJointsAttr().Get(&joints) || joints.empty())
        {
            fmt::print(stderr, fg(fmt::color::red), "UsdModelLoader: skeleton has no joints\n");
            return false;
        }

        VtMatrix4dArray bindXforms; // world-space bind
        VtMatrix4dArray restXforms; // local-space rest
        const bool hasBind = skel.GetBindTransformsAttr().Get(&bindXforms) && bindXforms.size() == joints.size();
        const bool hasRest = skel.GetRestTransformsAttr().Get(&restXforms) && restXforms.size() == joints.size();

        if (!hasBind)
        {
            fmt::print(stderr, fg(fmt::color::red), "UsdModelLoader: skeleton missing bindTransforms\n");
            return false;
        }

        // index of each joint path -> for parent lookup
        std::unordered_map<std::string, int> jointPathToIndex;
        for (size_t i = 0; i < joints.size(); ++i)
        {
            jointPathToIndex.emplace(joints[i].GetString(), static_cast<int>(i));
        }

        std::unordered_map<std::string, int> usedNames; // leaf name -> count, to disambiguate collisions
        usedNames.emplace(syntheticRootName, 1);

        // node 0 is the synthetic skeleton-space anchor; joint i lands at node i+1
        out.nodes.resize(joints.size() + 1);
        out.bones.resize(joints.size());
        out.nodes[0] = {syntheticRootName, -1, skeletonLocalToWorld};

        for (size_t i = 0; i < joints.size(); ++i)
        {
            const std::string jointPath = joints[i].GetString();

            // pick a unique, friendly node/bone name (leaf, full path on rare collision)
            std::string name = jointLeaf(jointPath);
            if (usedNames.find(name) != usedNames.end())
            {
                name = jointPath; // full path is guaranteed unique
            }
            usedNames.emplace(name, 1);

            jointToName.emplace(jointPath, name);
            jointToBoneId.emplace(jointPath, static_cast<int>(i));

            // parent: the joint whose path is the prefix, shifted by +1; root joints hang off the anchor
            int parentNodeIndex = 0;
            const std::string parentPath = jointParentPath(jointPath);
            if (!parentPath.empty())
            {
                const auto it = jointPathToIndex.find(parentPath);
                if (it != jointPathToIndex.end())
                {
                    parentNodeIndex = it->second + 1;
                }
            }

            // local rest transform; fall back to bind_i * inverse(bind_parent) when rest is absent
            glm::mat4 localTransform(1.0f);
            if (hasRest)
            {
                localTransform = glmFromGf(restXforms[i]);
            }
            else if (parentPath.empty() || jointPathToIndex.find(parentPath) == jointPathToIndex.end())
            {
                localTransform = glmFromGf(bindXforms[i]);
            }
            else
            {
                const int parentJoint = jointPathToIndex.at(parentPath);
                localTransform = glm::inverse(glmFromGf(bindXforms[static_cast<size_t>(parentJoint)])) *
                                 glmFromGf(bindXforms[i]);
            }

            out.nodes[i + 1] = {name, parentNodeIndex, localTransform};
            out.bones[i] = {name, glm::inverse(glmFromGf(bindXforms[i]))};
        }

        return true;
    }

    // ----- mesh ---------------------------------------------------------------------------------------

    // Pulls a flattened (de-indexed) primvar plus its interpolation. Returns false when absent.
    template <typename ArrayT>
    bool fetchPrimvar(const UsdGeomPrimvarsAPI& api, const TfToken& name, ArrayT& values, Interp& interp)
    {
        UsdGeomPrimvar pv = api.GetPrimvar(name);
        if (!pv || !pv.HasValue())
        {
            return false;
        }
        if (!pv.ComputeFlattened(&values))
        {
            return false;
        }
        interp = toInterp(pv.GetInterpolation());
        return true;
    }

    void buildMesh(const UsdGeomMesh& usdMesh,
                   const std::unordered_map<std::string, int>& jointToBoneId,
                   const std::string& diffuseTexName,
                   UsdLoadedModel& out)
    {
        const UsdPrim prim = usdMesh.GetPrim();

        VtVec3fArray points;
        VtIntArray faceVertexCounts;
        VtIntArray faceVertexIndices;
        usdMesh.GetPointsAttr().Get(&points);
        usdMesh.GetFaceVertexCountsAttr().Get(&faceVertexCounts);
        usdMesh.GetFaceVertexIndicesAttr().Get(&faceVertexIndices);

        if (points.empty() || faceVertexIndices.empty())
        {
            fmt::print(stderr, fg(fmt::color::red), "UsdModelLoader: mesh '{}' has no geometry\n",
                       prim.GetName().GetString());
            return;
        }

        UsdGeomPrimvarsAPI pvApi(prim);

        // normals: prefer primvars:normals, fall back to the Mesh normals attr
        VtVec3fArray normals;
        Interp normalsInterp = Interp::Vertex;
        bool hasNormals = fetchPrimvar(pvApi, TfToken("normals"), normals, normalsInterp);
        if (!hasNormals)
        {
            if (usdMesh.GetNormalsAttr().Get(&normals) && !normals.empty())
            {
                hasNormals = true;
                normalsInterp = toInterp(usdMesh.GetNormalsInterpolation());
            }
        }

        VtVec2fArray uvs;
        Interp uvInterp = Interp::FaceVarying;
        bool hasUv = fetchPrimvar(pvApi, TfToken("st"), uvs, uvInterp);

        VtVec3fArray displayColors;
        Interp colorInterp = Interp::Constant;
        const bool hasColor = fetchPrimvar(pvApi, TfToken("displayColor"), displayColors, colorInterp);

        // skinning influences (per point, vertex interpolation)
        UsdSkelBindingAPI binding(prim);
        VtIntArray jointIndices;
        VtFloatArray jointWeights;
        binding.GetJointIndicesAttr().Get(&jointIndices);
        binding.GetJointWeightsAttr().Get(&jointWeights);

        int influencesPerPoint = 0;
        UsdGeomPrimvar jiPv = binding.GetJointIndicesPrimvar();
        if (jiPv && jiPv.GetElementSize() > 0 && !points.empty())
        {
            influencesPerPoint = jiPv.GetElementSize();
        }
        else if (!jointIndices.empty() && !points.empty())
        {
            influencesPerPoint = static_cast<int>(jointIndices.size() / points.size());
        }

        // map the mesh-local joint order onto global bone ids (identity when the mesh omits skel:joints)
        VtTokenArray meshJoints;
        binding.GetJointsAttr().Get(&meshJoints);
        std::vector<int> localToBone(meshJoints.size(), 0);
        for (size_t i = 0; i < meshJoints.size(); ++i)
        {
            const auto it = jointToBoneId.find(meshJoints[i].GetString());
            localToBone[i] = it != jointToBoneId.end() ? it->second : 0;
        }
        const auto remapBone = [&](int localIndex) -> int
        {
            if (meshJoints.empty())
            {
                return localIndex; // mesh joint order already matches the skeleton
            }
            return localIndex >= 0 && localIndex < static_cast<int>(localToBone.size()) ? localToBone[localIndex] : 0;
        };

        // geomBindTransform applied to points/normals before skinning
        GfMatrix4d geomBind(1.0);
        binding.GetGeomBindTransformAttr().Get(&geomBind);
        const GfMatrix4d geomBindNormal = geomBind.GetInverse().GetTranspose();

        // Pre-transform points once (reused for vertices and normal generation).
        std::vector<glm::vec3> pointPositions(points.size());
        for (size_t i = 0; i < points.size(); ++i)
        {
            const GfVec3d p = geomBind.Transform(GfVec3d(points[i]));
            pointPositions[i] = glm::vec3(static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2]));
        }

        // These USD assets ship no authored normal values, so synthesize smooth (area-weighted) per-point
        // normals from the topology - equivalent to the old assimp aiProcess_GenNormals. Without this the
        // skinning shader's N.L term is 0 and the model renders solid black.
        std::vector<glm::vec3> generatedNormals;
        if (!hasNormals)
        {
            generatedNormals.assign(points.size(), glm::vec3(0.0f));
            int gc = 0;
            for (size_t f = 0; f < faceVertexCounts.size(); ++f)
            {
                const int count = faceVertexCounts[f];
                for (int k = 1; k + 1 < count; ++k)
                {
                    const int i0 = faceVertexIndices[static_cast<size_t>(gc)];
                    const int i1 = faceVertexIndices[static_cast<size_t>(gc + k)];
                    const int i2 = faceVertexIndices[static_cast<size_t>(gc + k + 1)];
                    const glm::vec3 faceNormal = glm::cross(pointPositions[i1] - pointPositions[i0],
                                                            pointPositions[i2] - pointPositions[i0]);
                    generatedNormals[i0] += faceNormal;
                    generatedNormals[i1] += faceNormal;
                    generatedNormals[i2] += faceNormal;
                }
                gc += count;
            }
            for (glm::vec3& n : generatedNormals)
            {
                n = glm::length(n) > 1e-8f ? glm::normalize(n) : glm::vec3(0.0f, 1.0f, 0.0f);
            }
        }

        RMesh mesh;
        mesh.usesPBRColors = true; // vertex/display colors drive shading; Draw binds the white texture
        if (!diffuseTexName.empty())
        {
            mesh.textures.insert({TextureType::Diffuse, diffuseTexName});
        }

        const auto buildVertex = [&](int faceIndex, int cornerIndex) -> RVertex
        {
            const int pointIndex = faceVertexIndices[static_cast<size_t>(cornerIndex)];

            RVertex v;

            v.position = pointPositions[static_cast<size_t>(pointIndex)];

            if (hasNormals)
            {
                const int ni = sampleIndex(normalsInterp, faceIndex, cornerIndex, pointIndex);
                if (ni >= 0 && ni < static_cast<int>(normals.size()))
                {
                    // GfMatrix4d::TransformDir returns GfVec3d (the matrix's scalar type)
                    const GfVec3d n = geomBindNormal.TransformDir(GfVec3d(normals[static_cast<size_t>(ni)]));
                    v.normal = glm::normalize(
                            glm::vec3(static_cast<float>(n[0]), static_cast<float>(n[1]), static_cast<float>(n[2])));
                }
            }
            else
            {
                v.normal = generatedNormals[static_cast<size_t>(pointIndex)];
            }

            if (hasUv)
            {
                const int ui = sampleIndex(uvInterp, faceIndex, cornerIndex, pointIndex);
                if (ui >= 0 && ui < static_cast<int>(uvs.size()))
                {
                    // flip V for Vulkan (matches the old assimp aiProcess_FlipUVs)
                    v.uv = glm::vec2(uvs[static_cast<size_t>(ui)][0], 1.0f - uvs[static_cast<size_t>(ui)][1]);
                }
            }

            if (hasColor)
            {
                const int ci = sampleIndex(colorInterp, faceIndex, cornerIndex, pointIndex);
                if (ci >= 0 && ci < static_cast<int>(displayColors.size()))
                {
                    const GfVec3f c = displayColors[static_cast<size_t>(ci)];
                    v.color = glm::vec4(c[0], c[1], c[2], 1.0f);
                }
            }

            // up to 4 bone influences from the per-point block
            if (influencesPerPoint > 0)
            {
                const int base = pointIndex * influencesPerPoint;
                for (int k = 0; k < influencesPerPoint && k < 4; ++k)
                {
                    const int idx = base + k;
                    if (idx < 0 || idx >= static_cast<int>(jointIndices.size()) ||
                        idx >= static_cast<int>(jointWeights.size()))
                    {
                        break;
                    }
                    v.boneNumber[k] = static_cast<uint32_t>(remapBone(jointIndices[static_cast<size_t>(idx)]));
                    v.boneWeight[k] = jointWeights[static_cast<size_t>(idx)];
                }

                const float sum = v.boneWeight.x + v.boneWeight.y + v.boneWeight.z + v.boneWeight.w;
                if (sum > 0.0f)
                {
                    v.boneWeight /= sum;
                }
            }

            return v;
        };

        // per-corner expansion + fan triangulation (robust against faceVarying normals/uv)
        int corner = 0;
        for (size_t f = 0; f < faceVertexCounts.size(); ++f)
        {
            const int count = faceVertexCounts[f];
            for (int k = 1; k + 1 < count; ++k)
            {
                const int c0 = corner;
                const int c1 = corner + k;
                const int c2 = corner + k + 1;

                const uint32_t startIndex = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.emplace_back(buildVertex(static_cast<int>(f), c0));
                mesh.vertices.emplace_back(buildVertex(static_cast<int>(f), c1));
                mesh.vertices.emplace_back(buildVertex(static_cast<int>(f), c2));
                mesh.indices.emplace_back(startIndex);
                mesh.indices.emplace_back(startIndex + 1);
                mesh.indices.emplace_back(startIndex + 2);
            }
            corner += count;
        }

        out.meshes.emplace_back(std::move(mesh));
    }

    // ----- animation ----------------------------------------------------------------------------------

    void loadClip(const std::string& assetDir,
                  const std::string& clipName,
                  const std::string& clipFile,
                  int startFrame,
                  int endFrame,
                  const std::unordered_map<std::string, std::string>& jointToName,
                  UsdLoadedModel& out)
    {
        const std::string clipPath = (std::filesystem::path(assetDir) / clipFile).generic_string();

        UsdStageRefPtr animStage = UsdStage::Open(clipPath);
        if (!animStage)
        {
            fmt::print(stderr, fg(fmt::color::red), "UsdModelLoader: could not open clip '{}'\n", clipPath);
            return;
        }

        // first SkelAnimation prim in the clip
        UsdSkelAnimation anim;
        for (const UsdPrim& prim : animStage->Traverse())
        {
            if (prim.IsA<UsdSkelAnimation>())
            {
                anim = UsdSkelAnimation(prim);
                break;
            }
        }
        if (!anim)
        {
            fmt::print(stderr, fg(fmt::color::red), "UsdModelLoader: clip '{}' has no SkelAnimation\n", clipPath);
            return;
        }

        VtTokenArray animJoints;
        anim.GetJointsAttr().Get(&animJoints);
        if (animJoints.empty())
        {
            return;
        }

        double fps = animStage->GetFramesPerSecond();
        if (fps <= 0.0)
        {
            fps = 24.0;
        }

        UsdAnimClipData clip;
        clip.name = clipName;
        clip.ticksPerSecond = static_cast<float>(fps);
        clip.duration = static_cast<float>(std::max(0, endFrame - startFrame));
        clip.channels.resize(animJoints.size());

        for (size_t j = 0; j < animJoints.size(); ++j)
        {
            const std::string jointPath = animJoints[j].GetString();
            const auto it = jointToName.find(jointPath);
            clip.channels[j].nodeName = it != jointToName.end() ? it->second : jointLeaf(jointPath);
        }

        // sample each integer frame in [start, end]; timings are clip-local (0..duration)
        for (int frame = startFrame; frame <= endFrame; ++frame)
        {
            const UsdTimeCode time(static_cast<double>(frame));
            VtVec3fArray translations;
            VtQuatfArray rotations;
            VtVec3hArray scales;
            anim.GetTranslationsAttr().Get(&translations, time);
            anim.GetRotationsAttr().Get(&rotations, time);
            anim.GetScalesAttr().Get(&scales, time);

            const float t = static_cast<float>(frame - startFrame);

            for (size_t j = 0; j < animJoints.size(); ++j)
            {
                UsdAnimChannelData& ch = clip.channels[j];

                if (j < translations.size())
                {
                    const GfVec3f& tr = translations[j];
                    ch.translationTimings.push_back(t);
                    ch.translations.push_back(glm::vec3(tr[0], tr[1], tr[2]));
                }
                if (j < rotations.size())
                {
                    const GfQuatf& q = rotations[j];
                    const GfVec3f im = q.GetImaginary();
                    ch.rotationTimings.push_back(t);
                    ch.rotations.push_back(glm::quat(q.GetReal(), im[0], im[1], im[2]));
                }
                if (j < scales.size())
                {
                    const GfVec3h& sc = scales[j];
                    ch.scaleTimings.push_back(t);
                    ch.scalings.push_back(
                            glm::vec3(static_cast<float>(sc[0]), static_cast<float>(sc[1]), static_cast<float>(sc[2])));
                }
            }
        }

        out.animClips.emplace_back(std::move(clip));
    }

    void loadClipsFromMetadata(const UsdStageRefPtr& stage,
                               const std::string& assetDir,
                               const std::unordered_map<std::string, std::string>& jointToName,
                               UsdLoadedModel& out)
    {
        // the asset layer records the clips index in customLayerData["clips"] (a path relative to the asset)
        const VtDictionary customData = stage->GetRootLayer()->GetCustomLayerData();
        const auto it = customData.find("clips");
        if (it == customData.end() || !it->second.IsHolding<std::string>())
        {
            fmt::print("UsdModelLoader: no clips metadata; loading static model\n");
            return;
        }

        const std::string clipsRel = it->second.Get<std::string>();
        const std::string clipsPath = (std::filesystem::path(assetDir) / clipsRel).generic_string();

        std::ifstream in(clipsPath, std::ios::binary);
        if (!in.is_open())
        {
            fmt::print(stderr, fg(fmt::color::red), "UsdModelLoader: could not open clips file '{}'\n", clipsPath);
            return;
        }
        const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        JsParseError parseError;
        const JsValue root = JsParseString(content, &parseError);
        if (!root.IsObject())
        {
            fmt::print(stderr, fg(fmt::color::red), "UsdModelLoader: failed to parse clips json '{}'\n", clipsPath);
            return;
        }

        const JsObject rootObj = root.GetJsObject();
        const auto clipsIt = rootObj.find("clips");
        if (clipsIt == rootObj.end() || !clipsIt->second.IsArray())
        {
            return;
        }

        for (const JsValue& clipVal : clipsIt->second.GetJsArray())
        {
            if (!clipVal.IsObject())
            {
                continue;
            }
            const JsObject clip = clipVal.GetJsObject();

            const auto nameIt = clip.find("name");
            const auto fileIt = clip.find("file");
            const auto startIt = clip.find("start");
            const auto endIt = clip.find("end");
            if (fileIt == clip.end() || !fileIt->second.IsString())
            {
                continue;
            }

            const std::string name = nameIt != clip.end() && nameIt->second.IsString() ? nameIt->second.GetString()
                                                                                       : fileIt->second.GetString();
            const std::string file = fileIt->second.GetString();
            const int start = startIt != clip.end() ? startIt->second.GetInt() : 0;
            const int end = endIt != clip.end() ? endIt->second.GetInt() : 0;

            loadClip(assetDir, name, file, start, end, jointToName, out);
        }
    }

    // ----- material -----------------------------------------------------------------------------------

    // Best-effort diffuse texture: the first UsdUVTexture whose 'file' actually exists on disk. Many of
    // these assets bake an absolute authoring-machine path that is absent at runtime, so existence-check.
    std::string findDiffuseTexture(const UsdStageRefPtr& stage, UsdLoadedModel& out)
    {
        for (const UsdPrim& prim : stage->Traverse())
        {
            if (!prim.IsA<UsdShadeShader>())
            {
                continue;
            }
            UsdShadeShader shader(prim);
            TfToken id;
            shader.GetIdAttr().Get(&id);
            if (id != TfToken("UsdUVTexture"))
            {
                continue;
            }

            UsdShadeInput fileInput = shader.GetInput(TfToken("file"));
            SdfAssetPath assetPath;
            if (!fileInput || !fileInput.Get(&assetPath))
            {
                continue;
            }

            std::string resolved = assetPath.GetResolvedPath();
            if (resolved.empty())
            {
                resolved = assetPath.GetAssetPath();
            }
            if (resolved.empty() || !std::filesystem::exists(resolved))
            {
                continue;
            }

            const std::string name = std::filesystem::path(resolved).filename().generic_string();
            out.textures.push_back({name, resolved});
            return name;
        }
        return std::string();
    }
} // namespace

namespace RAnimation
{
    bool LoadUsdModel(const std::string& assetUsdPath, UsdLoadedModel& out)
    {
        detail::RegisterUsdPluginsOnce();

        UsdStageRefPtr stage = UsdStage::Open(assetUsdPath);
        if (!stage)
        {
            fmt::print(stderr, fg(fmt::color::red), "UsdModelLoader: could not open stage '{}'\n", assetUsdPath);
            return false;
        }

        // locate the first Skeleton and the first Mesh in the composed stage
        UsdSkelSkeleton skeleton;
        UsdGeomMesh mesh;
        for (const UsdPrim& prim : stage->Traverse())
        {
            if (!skeleton && prim.IsA<UsdSkelSkeleton>())
            {
                skeleton = UsdSkelSkeleton(prim);
            }
            if (!mesh && prim.IsA<UsdGeomMesh>())
            {
                mesh = UsdGeomMesh(prim);
            }
        }

        if (!skeleton)
        {
            fmt::print(stderr, fg(fmt::color::red), "UsdModelLoader: no UsdSkel Skeleton found in '{}'\n", assetUsdPath);
            return false;
        }

        // skeleton-local -> world (SkelRoot/Armature orientation), carried by the synthetic root node.
        // Also fold in the stage's metersPerUnit so the model renders in metres: these assets are authored
        // in centimetres (no metersPerUnit -> USD fallback 0.01), which otherwise makes the model ~100x too
        // big. A uniform scale at the root converts the whole skinned result to metres.
        UsdGeomXformCache xformCache(UsdTimeCode::Default());
        double metersPerUnit = UsdGeomGetStageMetersPerUnit(stage);
        if (metersPerUnit <= 0.0)
        {
            metersPerUnit = 1.0;
        }
        glm::mat4 skeletonLocalToWorld = glmFromGf(xformCache.GetLocalToWorldTransform(skeleton.GetPrim()));
        skeletonLocalToWorld =
                glm::scale(glm::mat4(1.0f), glm::vec3(static_cast<float>(metersPerUnit))) * skeletonLocalToWorld;
        const std::string syntheticRootName = skeleton.GetPrim().GetPath().GetString();

        std::unordered_map<std::string, std::string> jointToName;
        std::unordered_map<std::string, int> jointToBoneId;
        if (!buildSkeleton(skeleton, skeletonLocalToWorld, syntheticRootName, out, jointToName, jointToBoneId))
        {
            return false;
        }

        const std::string diffuseTexName = findDiffuseTexture(stage, out);

        if (mesh)
        {
            buildMesh(mesh, jointToBoneId, diffuseTexName, out);
        }
        else
        {
            fmt::print(stderr, fg(fmt::color::red), "UsdModelLoader: no Mesh found in '{}'\n", assetUsdPath);
        }

        const std::string assetDir = std::filesystem::path(assetUsdPath).parent_path().generic_string();
        loadClipsFromMetadata(stage, assetDir, jointToName, out);

        out.rootTransform = glm::mat4(1.0f);

        fmt::print("UsdModelLoader: loaded '{}' - {} joints, {} meshes, {} clips\n",
                   assetUsdPath,
                   out.nodes.size(),
                   out.meshes.size(),
                   out.animClips.size());
        return true;
    }
} // namespace RAnimation
