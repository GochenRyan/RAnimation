#include <AnimGraph/GraphDesc.h>

namespace RAnimation
{
    bool IsGpuCapableNodeType(const std::string& type)
    {
        return type == "Root" || type == "ClipPlayer" || type == "TwoWayBlend" || type == "BlendSpace1D";
    }

    bool IsKnownNodeType(const std::string& type)
    {
        return IsGpuCapableNodeType(type) || type == "SaveCachedPose" || type == "UseCachedPose" || type == "CpuOnly";
    }

    std::vector<int> FoldCpuOnly(GraphDesc& desc)
    {
        const int n = static_cast<int>(desc.Nodes.size());

        // Resolve chains of CpuOnly markers to their ultimate source and flag that source.
        std::vector<int> resolved(n, -1);
        for (int i = 0; i < n; ++i)
        {
            int cur = i;
            int guard = 0;
            while (cur >= 0 && cur < n && desc.Nodes[cur].Type == "CpuOnly" && guard++ < n)
            {
                cur = desc.Nodes[cur].FindInput("Source");
            }
            resolved[i] = cur;
            if (cur >= 0 && cur < n && cur != i)
            {
                desc.Nodes[cur].bForceCpu = true;
            }
        }

        // Rewire consumers (including the output node) past the markers.
        for (NodeDesc& node : desc.Nodes)
        {
            for (auto& [pin, src] : node.Inputs)
            {
                if (src >= 0 && src < n)
                {
                    src = resolved[src];
                }
            }
        }
        if (desc.OutputNode >= 0 && desc.OutputNode < n)
        {
            desc.OutputNode = resolved[desc.OutputNode];
        }

        // Compact away the markers.
        std::vector<int> remap(n, -1);
        GraphDesc folded;
        for (int i = 0; i < n; ++i)
        {
            if (desc.Nodes[i].Type == "CpuOnly")
            {
                continue;
            }
            remap[i] = folded.AddNode(desc.Nodes[i]);
        }
        for (NodeDesc& node : folded.Nodes)
        {
            for (auto& [pin, src] : node.Inputs)
            {
                src = (src >= 0 && src < n) ? remap[src] : -1;
            }
        }
        folded.OutputNode = (desc.OutputNode >= 0 && desc.OutputNode < n) ? remap[desc.OutputNode] : -1;
        desc = std::move(folded);
        return remap;
    }

    GraphDesc MakeSingleClipGraphDesc(int clipIndex, float playRate, bool looping)
    {
        GraphDesc desc;
        NodeDesc clip;
        clip.Type = "ClipPlayer";
        clip.ClipIndex = clipIndex;
        clip.PlayRate = playRate;
        clip.bLooping = looping;
        const int clipId = desc.AddNode(clip);

        NodeDesc root;
        root.Type = "Root";
        root.Inputs.emplace_back("Result", clipId);
        desc.OutputNode = desc.AddNode(root);
        return desc;
    }
} // namespace RAnimation
