// Third (and last) pxr translation unit of the sample: .animgraph.usda I/O. Keep every USD header here.
#include <AnimGraph/AnimGraphUsdIo.h>

#include <algorithm>
#include <map>

#include <fmt/format.h>

#include "../Model/UsdPluginRegistration.h"

#include <pxr/pxr.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/dictionary.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/relationship.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/scope.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace RAnimation
{
    namespace
    {
        const char* kRootPath = "/AnimGraph";

        std::string NodePrimName(int index)
        {
            return "Node_" + std::to_string(index);
        }

        int NodeIndexFromPrimName(const std::string& name)
        {
            if (name.rfind("Node_", 0) != 0)
            {
                return -1;
            }
            return std::atoi(name.c_str() + 5);
        }

        template <typename T>
        void SetCustomAttr(UsdPrim& prim, const char* name, const SdfValueTypeName& type, const T& value)
        {
            UsdAttribute attr = prim.CreateAttribute(TfToken(name), type, /*custom*/ true);
            attr.Set(VtValue(value));
        }

        template <typename T>
        bool GetAttr(const UsdPrim& prim, const char* name, T& out)
        {
            UsdAttribute attr = prim.GetAttribute(TfToken(name));
            return attr && attr.Get(&out);
        }
    } // namespace

    bool SaveAnimGraphUsd(const GraphDesc& editorDesc,
                          const GraphLayout& layout,
                          const std::string& doc,
                          const std::string& path,
                          std::string& outError)
    {
        detail::RegisterUsdPluginsOnce();

        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        if (!stage)
        {
            outError = "could not create a USD stage";
            return false;
        }

        UsdPrim root = UsdGeomScope::Define(stage, SdfPath(kRootPath)).GetPrim();
        stage->SetDefaultPrim(root);
        SetCustomAttr(root, "ranim:outputNode", SdfValueTypeNames->Int, editorDesc.OutputNode);

        VtDictionary ranim;
        ranim["animGraphVersion"] = VtValue(kAnimGraphFormatVersion);
        ranim["doc"] = VtValue(doc);
        VtDictionary layerData;
        layerData["ranim"] = VtValue(ranim);
        stage->GetRootLayer()->SetCustomLayerData(layerData);

        for (size_t i = 0; i < editorDesc.Nodes.size(); ++i)
        {
            const NodeDesc& d = editorDesc.Nodes[i];
            const SdfPath primPath = SdfPath(kRootPath).AppendChild(TfToken(NodePrimName(static_cast<int>(i))));
            UsdPrim prim = UsdGeomScope::Define(stage, primPath).GetPrim();

            SetCustomAttr(prim, "ranim:type", SdfValueTypeNames->String, d.Type);
            if (d.Type == "ClipPlayer")
            {
                SetCustomAttr(prim, "ranim:clipIndex", SdfValueTypeNames->Int, d.ClipIndex);
                SetCustomAttr(prim, "ranim:playRate", SdfValueTypeNames->Float, d.PlayRate);
                SetCustomAttr(prim, "ranim:looping", SdfValueTypeNames->Bool, d.bLooping);
            }
            else if (d.Type == "TwoWayBlend" || d.Type == "BlendSpace1D")
            {
                SetCustomAttr(prim, "ranim:param", SdfValueTypeNames->Float, d.Param);
            }
            if (d.Type == "BlendSpace1D" && !d.SamplePositions.empty())
            {
                VtFloatArray positions(d.SamplePositions.begin(), d.SamplePositions.end());
                SetCustomAttr(prim, "ranim:samplePositions", SdfValueTypeNames->FloatArray, positions);
            }
            if (!d.CacheName.empty())
            {
                SetCustomAttr(prim, "ranim:cacheName", SdfValueTypeNames->String, d.CacheName);
            }
            if (i < layout.NodePos.size())
            {
                SetCustomAttr(prim, "ranim:editorPos", SdfValueTypeNames->Float2, GfVec2f(layout.NodePos[i].x, layout.NodePos[i].y));
            }

            for (const auto& [pin, src] : d.Inputs)
            {
                if (src < 0 || static_cast<size_t>(src) >= editorDesc.Nodes.size())
                {
                    continue;
                }
                UsdRelationship rel = prim.CreateRelationship(TfToken("ranim:in:" + pin), /*custom*/ true);
                rel.SetTargets({SdfPath(kRootPath).AppendChild(TfToken(NodePrimName(src)))});
            }
        }

        if (!stage->GetRootLayer()->Export(path))
        {
            outError = fmt::format("could not write '{}'", path);
            return false;
        }
        return true;
    }

    bool LoadAnimGraphUsd(const std::string& path,
                          GraphDesc& outEditorDesc,
                          GraphLayout& outLayout,
                          std::string& outDoc,
                          std::string& outError)
    {
        detail::RegisterUsdPluginsOnce();

        outEditorDesc = GraphDesc{};
        outLayout = GraphLayout{};
        outDoc.clear();

        UsdStageRefPtr stage = UsdStage::Open(path);
        if (!stage)
        {
            outError = fmt::format("could not open '{}'", path);
            return false;
        }

        const VtDictionary layerData = stage->GetRootLayer()->GetCustomLayerData();
        const VtValue* version = layerData.GetValueAtPath("ranim:animGraphVersion");
        if (version == nullptr || !version->IsHolding<int>())
        {
            outError = fmt::format("'{}' is not an animgraph layer (missing ranim.animGraphVersion)", path);
            return false;
        }
        if (version->Get<int>() != kAnimGraphFormatVersion)
        {
            outError = fmt::format("'{}' has animgraph version {} (expected {})", path, version->Get<int>(), kAnimGraphFormatVersion);
            return false;
        }
        if (const VtValue* doc = layerData.GetValueAtPath("ranim:doc"); doc != nullptr && doc->IsHolding<std::string>())
        {
            outDoc = doc->Get<std::string>();
        }

        UsdPrim root = stage->GetPrimAtPath(SdfPath(kRootPath));
        if (!root)
        {
            root = stage->GetDefaultPrim();
        }
        if (!root)
        {
            outError = fmt::format("'{}' has no /AnimGraph prim", path);
            return false;
        }

        // Collect nodes by index first (prim order is alphabetical, Node_10 < Node_2).
        std::map<int, UsdPrim> prims;
        for (const UsdPrim& child : root.GetChildren())
        {
            const int index = NodeIndexFromPrimName(child.GetName().GetString());
            if (index >= 0)
            {
                prims[index] = child;
            }
        }
        if (prims.empty())
        {
            outError = fmt::format("'{}' contains no nodes", path);
            return false;
        }
        const int count = prims.rbegin()->first + 1;
        outEditorDesc.Nodes.resize(static_cast<size_t>(count));
        outLayout.NodePos.assign(static_cast<size_t>(count), glm::vec2(0.0f));

        std::map<std::string, int> indexByPrimName;
        for (const auto& [index, prim] : prims)
        {
            indexByPrimName[prim.GetName().GetString()] = index;
        }

        for (const auto& [index, prim] : prims)
        {
            NodeDesc& d = outEditorDesc.Nodes[static_cast<size_t>(index)];
            GetAttr(prim, "ranim:type", d.Type);
            GetAttr(prim, "ranim:clipIndex", d.ClipIndex);
            GetAttr(prim, "ranim:param", d.Param);
            GetAttr(prim, "ranim:playRate", d.PlayRate);
            GetAttr(prim, "ranim:looping", d.bLooping);
            GetAttr(prim, "ranim:cacheName", d.CacheName);
            VtFloatArray positions;
            if (GetAttr(prim, "ranim:samplePositions", positions))
            {
                d.SamplePositions.assign(positions.begin(), positions.end());
            }
            GfVec2f pos;
            if (GetAttr(prim, "ranim:editorPos", pos))
            {
                outLayout.NodePos[static_cast<size_t>(index)] = glm::vec2(pos[0], pos[1]);
            }

            for (const UsdRelationship& rel : prim.GetRelationships())
            {
                const std::string name = rel.GetName().GetString();
                if (name.rfind("ranim:in:", 0) != 0)
                {
                    continue;
                }
                SdfPathVector targets;
                rel.GetTargets(&targets);
                int src = -1;
                if (!targets.empty())
                {
                    const auto it = indexByPrimName.find(targets.front().GetName());
                    src = it != indexByPrimName.end() ? it->second : -1;
                }
                d.Inputs.emplace_back(name.substr(9), src);
            }
        }

        if (!GetAttr(root, "ranim:outputNode", outEditorDesc.OutputNode))
        {
            for (size_t i = 0; i < outEditorDesc.Nodes.size(); ++i)
            {
                if (outEditorDesc.Nodes[i].Type == "Root")
                {
                    outEditorDesc.OutputNode = static_cast<int>(i);
                }
            }
        }
        return true;
    }
} // namespace RAnimation
