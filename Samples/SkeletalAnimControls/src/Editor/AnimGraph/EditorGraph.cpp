#include <Editor/AnimGraph/EditorGraph.h>

#include <algorithm>
#include <functional>
#include <map>

#include <fmt/format.h>

#include <AnimGraph/GraphBaker.h>
#include <Editor/AnimGraph/GraphModel.h>

namespace RAnimation
{
    namespace
    {
        const PinDesc* FindPin(const std::vector<PinDesc>& pins, const std::string& name)
        {
            for (const PinDesc& p : pins)
            {
                if (p.Name == name)
                {
                    return &p;
                }
            }
            return nullptr;
        }
    } // namespace

    int EditorGraph::AddNode(const std::string& Type, float X, float Y)
    {
        const NodeTypeDesc* desc = FindNodeType(Type);
        EditorNode node;
        node.Id = NextId++;
        node.Type = Type;
        node.PosX = X;
        node.PosY = Y;
        node.Param = desc != nullptr ? desc->ParamDefault : 0.0f;
        Nodes.push_back(node);
        return node.Id;
    }

    EditorNode* EditorGraph::FindNode(int Id)
    {
        for (EditorNode& n : Nodes)
        {
            if (n.Id == Id)
            {
                return &n;
            }
        }
        return nullptr;
    }

    const EditorNode* EditorGraph::FindNode(int Id) const
    {
        for (const EditorNode& n : Nodes)
        {
            if (n.Id == Id)
            {
                return &n;
            }
        }
        return nullptr;
    }

    const EditorLink* EditorGraph::FindLinkTo(int ToNode, const std::string& ToPin) const
    {
        for (const EditorLink& l : Links)
        {
            if (l.ToNode == ToNode && l.ToPin == ToPin)
            {
                return &l;
            }
        }
        return nullptr;
    }

    EditorGraph::LinkCheck EditorGraph::CanCreateLink(int FromNode, const std::string& FromPin, int ToNode, const std::string& ToPin) const
    {
        LinkCheck check;
        const EditorNode* from = FindNode(FromNode);
        const EditorNode* to = FindNode(ToNode);
        if (from == nullptr || to == nullptr)
        {
            check.Reason = "unknown node";
            return check;
        }
        if (FromNode == ToNode)
        {
            check.Reason = "cannot connect a node to itself";
            return check;
        }
        const NodeTypeDesc* fromType = FindNodeType(from->Type);
        const NodeTypeDesc* toType = FindNodeType(to->Type);
        if (fromType == nullptr || toType == nullptr)
        {
            check.Reason = "unknown node type";
            return check;
        }
        const PinDesc* out = FindPin(fromType->Outputs, FromPin);
        const PinDesc* in = FindPin(toType->Inputs, ToPin);
        if (out == nullptr)
        {
            check.Reason = fmt::format("{} has no output '{}'", from->Type, FromPin);
            return check;
        }
        if (in == nullptr)
        {
            check.Reason = fmt::format("{} has no input '{}'", to->Type, ToPin);
            return check;
        }
        if (out->Type != in->Type)
        {
            check.Reason = fmt::format("pin types differ ({} -> {})", PinTypeName(out->Type), PinTypeName(in->Type));
            return check;
        }
        if (WouldCreateCycle(FromNode, ToNode))
        {
            check.Reason = "would create a cycle";
            return check;
        }
        check.bOk = true;
        return check;
    }

    int EditorGraph::AddLink(int FromNode, const std::string& FromPin, int ToNode, const std::string& ToPin)
    {
        if (!CanCreateLink(FromNode, FromPin, ToNode, ToPin).bOk)
        {
            return -1;
        }
        // An input accepts one link: replace the existing one.
        Links.erase(std::remove_if(Links.begin(), Links.end(), [&](const EditorLink& l) { return l.ToNode == ToNode && l.ToPin == ToPin; }),
                    Links.end());
        EditorLink link;
        link.Id = NextId++;
        link.FromNode = FromNode;
        link.FromPin = FromPin;
        link.ToNode = ToNode;
        link.ToPin = ToPin;
        Links.push_back(link);
        return link.Id;
    }

    void EditorGraph::RemoveLink(int LinkId)
    {
        Links.erase(std::remove_if(Links.begin(), Links.end(), [&](const EditorLink& l) { return l.Id == LinkId; }), Links.end());
    }

    void EditorGraph::RemoveNode(int NodeId)
    {
        Links.erase(std::remove_if(Links.begin(), Links.end(), [&](const EditorLink& l) { return l.FromNode == NodeId || l.ToNode == NodeId; }),
                    Links.end());
        Nodes.erase(std::remove_if(Nodes.begin(), Nodes.end(), [&](const EditorNode& n) { return n.Id == NodeId; }), Nodes.end());
    }

    bool EditorGraph::WouldCreateCycle(int FromNode, int ToNode) const
    {
        // Does ToNode already feed (directly or indirectly) into FromNode? Walk from FromNode along its inputs.
        std::vector<int> stack = {FromNode};
        std::vector<int> seen;
        while (!stack.empty())
        {
            const int cur = stack.back();
            stack.pop_back();
            if (cur == ToNode)
            {
                return true;
            }
            if (std::find(seen.begin(), seen.end(), cur) != seen.end())
            {
                continue;
            }
            seen.push_back(cur);
            for (const EditorLink& l : Links)
            {
                if (l.ToNode == cur)
                {
                    stack.push_back(l.FromNode);
                }
            }
            // UseCachedPose depends on its SaveCachedPose (virtual edge)
            if (const EditorNode* n = FindNode(cur); n != nullptr && n->Type == "UseCachedPose")
            {
                for (const EditorNode& other : Nodes)
                {
                    if (other.Type == "SaveCachedPose" && other.CacheName == n->CacheName)
                    {
                        stack.push_back(other.Id);
                    }
                }
            }
        }
        return false;
    }

    std::vector<std::string> EditorGraph::Validate(int clipCount) const
    {
        std::vector<std::string> errors;
        int rootCount = 0;
        for (const EditorNode& n : Nodes)
        {
            if (FindNodeType(n.Type) == nullptr)
            {
                errors.push_back(fmt::format("node {}: unknown type '{}'", n.Id, n.Type));
            }
            if (n.Type == "Root")
            {
                ++rootCount;
            }
            if (n.Type == "CpuOnly" && FindLinkTo(n.Id, "Source") == nullptr)
            {
                errors.push_back(fmt::format("node {} (CpuOnly): Source is not connected", n.Id));
            }
        }
        if (rootCount != 1)
        {
            errors.push_back(fmt::format("graph must have exactly one Root (found {})", rootCount));
        }
        if (!errors.empty())
        {
            return errors;
        }

        GraphDesc desc = ToGraphDesc();
        FoldCpuOnly(desc);
        const std::vector<std::string> bakerErrors = GraphBaker::Validate(desc, clipCount);
        errors.insert(errors.end(), bakerErrors.begin(), bakerErrors.end());
        return errors;
    }

    GraphDesc EditorGraph::ToGraphDesc(std::vector<int>* outIdByIndex) const
    {
        GraphDesc desc;
        std::map<int, int> indexById;
        for (size_t i = 0; i < Nodes.size(); ++i)
        {
            indexById[Nodes[i].Id] = static_cast<int>(i);
        }
        if (outIdByIndex != nullptr)
        {
            outIdByIndex->clear();
        }

        for (const EditorNode& n : Nodes)
        {
            NodeDesc d;
            d.Type = n.Type;
            d.ClipIndex = n.ClipIndex;
            d.CacheName = n.CacheName;
            d.bLooping = n.bLooping;
            if (n.Type == "ClipPlayer")
            {
                d.PlayRate = n.Param; // the editor's Rate slider IS the runtime PlayRate
                d.Param = 0.0f;
            }
            else
            {
                d.Param = n.Param;
            }
            if (n.Type == "BlendSpace1D")
            {
                // Three evenly spaced samples on [0, 1]; the pins decide how many are actually connected.
                d.SamplePositions = {0.0f, 0.5f, 1.0f};
            }
            for (const EditorLink& l : Links)
            {
                if (l.ToNode == n.Id)
                {
                    const auto it = indexById.find(l.FromNode);
                    d.Inputs.emplace_back(l.ToPin, it != indexById.end() ? it->second : -1);
                }
            }
            desc.AddNode(d);
            if (outIdByIndex != nullptr)
            {
                outIdByIndex->push_back(n.Id);
            }
            if (n.Type == "Root")
            {
                desc.OutputNode = static_cast<int>(desc.Nodes.size()) - 1;
            }
        }
        return desc;
    }

    GraphLayout EditorGraph::ToLayout() const
    {
        GraphLayout layout;
        for (const EditorNode& n : Nodes)
        {
            layout.NodePos.emplace_back(n.PosX, n.PosY);
        }
        return layout;
    }

    void EditorGraph::FromGraphDesc(const GraphDesc& desc, const GraphLayout& layout)
    {
        Nodes.clear();
        Links.clear();
        NextId = 1;
        std::vector<int> idByIndex(desc.Nodes.size(), 0);
        for (size_t i = 0; i < desc.Nodes.size(); ++i)
        {
            const NodeDesc& d = desc.Nodes[i];
            EditorNode n;
            n.Id = NextId++;
            n.Type = d.Type;
            n.ClipIndex = d.ClipIndex;
            n.CacheName = d.CacheName;
            n.bLooping = d.bLooping;
            n.Param = d.Type == "ClipPlayer" ? d.PlayRate : d.Param;
            if (i < layout.NodePos.size())
            {
                n.PosX = layout.NodePos[i].x;
                n.PosY = layout.NodePos[i].y;
            }
            else
            {
                n.PosX = 80.0f + 260.0f * static_cast<float>(i % 4);
                n.PosY = 80.0f + 180.0f * static_cast<float>(i / 4);
            }
            idByIndex[i] = n.Id;
            Nodes.push_back(n);
        }
        for (size_t i = 0; i < desc.Nodes.size(); ++i)
        {
            for (const auto& [pin, src] : desc.Nodes[i].Inputs)
            {
                if (src < 0 || static_cast<size_t>(src) >= desc.Nodes.size())
                {
                    continue;
                }
                EditorLink l;
                l.Id = NextId++;
                l.FromNode = idByIndex[static_cast<size_t>(src)];
                l.FromPin = "Pose";
                l.ToNode = idByIndex[i];
                l.ToPin = pin;
                Links.push_back(l);
            }
        }
    }

    void EditorGraph::SyncToAsset(AnimGraphAsset& asset) const
    {
        asset.EditorDesc = ToGraphDesc();
        asset.Layout = ToLayout();
        asset.Finalize();
    }
} // namespace RAnimation
