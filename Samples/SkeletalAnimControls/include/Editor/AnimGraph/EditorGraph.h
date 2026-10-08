#pragma once
#include <string>
#include <vector>

#include <AnimGraph/AnimGraphAsset.h>
#include <AnimGraph/GraphDesc.h>

namespace RAnimation
{
    struct EditorNode
    {
        int Id = 0;
        std::string Type;
        float PosX = 0;
        float PosY = 0;
        int ClipIndex = 0;
        float Param = 0.0f; // TwoWayBlend.Alpha / BlendSpace1D.Input / ClipPlayer rate (mapped to PlayRate)
        bool bLooping = true;
        std::string CacheName;
    };

    struct EditorLink
    {
        int Id = 0;
        int FromNode = 0;
        std::string FromPin;
        int ToNode = 0;
        std::string ToPin;
    };

    // Editor graph model: nodes, links, validity, cycle prevention and the conversion to/from the runtime
    // GraphDesc. GUI-free.
    class EditorGraph
    {
    public:
        int AddNode(const std::string& Type, float X = 0, float Y = 0);
        EditorNode* FindNode(int Id);
        const EditorNode* FindNode(int Id) const;
        const EditorLink* FindLinkTo(int ToNode, const std::string& ToPin) const;

        // Link validity: pin types must match, inputs accept one link, no cycles.
        struct LinkCheck
        {
            bool bOk = false;
            std::string Reason;
        };
        LinkCheck CanCreateLink(int FromNode, const std::string& FromPin, int ToNode, const std::string& ToPin) const;
        // Adds the link (replacing whatever was connected to ToPin). Returns the new link id or -1.
        int AddLink(int FromNode, const std::string& FromPin, int ToNode, const std::string& ToPin);
        void RemoveLink(int LinkId);
        void RemoveNode(int NodeId);

        // A new link means ToNode depends on FromNode, so it closes a cycle iff FromNode already depends
        // (transitively) on ToNode. Walks from FromNode along "input -> source".
        bool WouldCreateCycle(int FromNode, int ToNode) const;
        // Editor-level rules, then the folded description goes through GraphBaker::Validate.
        std::vector<std::string> Validate(int clipCount) const;

        // Translates editor ids into array indices - the seam between editor and runtime. Returns the
        // editor-level description (CpuOnly included); outIdByIndex[i] is the editor id of node i.
        GraphDesc ToGraphDesc(std::vector<int>* outIdByIndex = nullptr) const;
        GraphLayout ToLayout() const;
        void FromGraphDesc(const GraphDesc& desc, const GraphLayout& layout);

        // Writes EditorDesc/Layout into the asset and re-finalises it (bumps Revision).
        void SyncToAsset(AnimGraphAsset& asset) const;

        std::vector<EditorNode> Nodes;
        std::vector<EditorLink> Links;
        int NextId = 1;
    };
} // namespace RAnimation
