#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace RAnimation
{
    // Serializable graph description (editor output, baker input). Node ids are indices into Nodes; links
    // are recorded on the consumer (pin name -> source node index).
    struct NodeDesc
    {
        std::string Type;                                  // Root / ClipPlayer / TwoWayBlend / BlendSpace1D / SaveCachedPose / UseCachedPose / CpuOnly
        std::string Name;                                  // optional, unique when set; AnimGraphInstance::FindNode (ranim:name)
        int ClipIndex = 0;                                 // ClipPlayer: used when ClipName is empty (or unknown to the model)
        std::string ClipName;                              // ClipPlayer: resolved against the model at bake (ranim:clipName)
        float Param = 0.0f;                                // TwoWayBlend.Alpha / BlendSpace1D.Input / LayeredBlend.Alpha
        float PlayRate = 1.0f;                             // ClipPlayer: the only runtime rate (the editor's Param maps onto it)
        bool bLooping = true;                              // ClipPlayer: the only runtime looping switch
        std::string CacheName;                             // SaveCachedPose / UseCachedPose
        int ActiveIndex = 0;                               // CrossfadeSelect (ranim:activeIndex)
        float BlendTimeSec = 0.2f;                         // CrossfadeSelect (ranim:blendTime)
        std::vector<std::string> InputNames;               // CrossfadeSelect: names of In0..InN (ranim:inputNames)
        std::string SlotName;                              // AnimSlot (ranim:slotName)
        std::string MaskRoot;                              // LayeredBlend: node whose subtree the layer covers (ranim:maskRoot)
        int BlendDepth = 0;                                // LayeredBlend: generations that ramp in (ranim:blendDepth)
        std::vector<float> SamplePositions;                // BlendSpace1D, ascending
        std::vector<float> SamplePositions2;               // BlendSpace2D: x0, y0, x1, y1, ... (ranim:samplePositions2)
        float Param2 = 0.0f;                               // BlendSpace2D.InputY (ranim:param2); Param is InputX
        bool bWrapX = true;                                // BlendSpace2D: x is a ring of 360 (ranim:wrapX)
        std::vector<std::pair<std::string, int>> Inputs;   // pin name -> source node index (-1 = unconnected)
        bool bForceCpu = false;                            // set on the source of a folded CpuOnly marker

        int FindInput(const std::string& pin) const
        {
            for (const auto& [name, idx] : Inputs)
            {
                if (name == pin)
                {
                    return idx;
                }
            }
            return -1;
        }
    };

    struct GraphDesc
    {
        std::vector<NodeDesc> Nodes;
        int OutputNode = -1; // the Root node

        int AddNode(const NodeDesc& d)
        {
            Nodes.push_back(d);
            return static_cast<int>(Nodes.size()) - 1;
        }

        int FindNodeByName(std::string_view name) const
        {
            if (name.empty())
            {
                return -1;
            }
            for (size_t i = 0; i < Nodes.size(); ++i)
            {
                if (Nodes[i].Name == name)
                {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }
    };

    // Node types the GPU kernel can evaluate (pure sample + weighted blend, no two-pass protocol).
    bool IsGpuCapableNodeType(const std::string& type);
    bool IsKnownNodeType(const std::string& type);

    // Removes CpuOnly pass-through markers: consumers are rewired to the marker's Source and that Source
    // gets bForceCpu = true (the CPU property then propagates upstream in ExecPlan). Returns the
    // old-index -> new-index map (-1 for removed nodes) so editors can map runtime results back.
    std::vector<int> FoldCpuOnly(GraphDesc& desc);

    // The implicit graph used by instances without an authored graph: ClipPlayer -> Root.
    GraphDesc MakeSingleClipGraphDesc(int clipIndex, float playRate, bool looping);
} // namespace RAnimation
