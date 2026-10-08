#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// AnimGraph editor model: the node type table. GUI-free. The runtime counterparts are
// AnimGraph/GraphDesc.h (IsGpuCapableNodeType) and the baker's pin table; keep the three in sync.

namespace RAnimation
{
    // Pin type: the basis for link validity.
    enum class PinType
    {
        Pose,
        Float,
        Bool
    };

    inline std::string_view PinTypeName(PinType T)
    {
        switch (T)
        {
        case PinType::Pose:
            return "Pose";
        case PinType::Float:
            return "Float";
        default:
            return "Bool";
        }
    }

    inline std::optional<PinType> PinTypeFromName(std::string_view S)
    {
        if (S == "Pose")
            return PinType::Pose;
        if (S == "Float")
            return PinType::Float;
        if (S == "Bool")
            return PinType::Bool;
        return std::nullopt;
    }

    struct PinDesc
    {
        std::string Name;
        PinType Type;
    };

    // Describes how a node type looks and behaves in the editor.
    struct NodeTypeDesc
    {
        std::string TypeName;
        std::vector<PinDesc> Inputs;
        std::vector<PinDesc> Outputs;
        bool bHasClipIndex = false;
        bool bHasParam = false;
        bool bHasCacheName = false;

        // Whether Evaluate can run in the compute shader. It must be a pure function: sample + weighted
        // blend, no branch state, no side effects, no external inputs (colliders, IK targets) and no
        // cross-node two-pass protocol. Save/UseCachedPose are false because their "evaluate once at the
        // end of Update, reuse" protocol cannot be expressed on the GPU.
        bool bCanEvaluateOnGPU = false;

        // Param meaning depends on the type (TwoWayBlend Alpha, BlendSpace1D Input, ClipPlayer rate); the
        // default, slider maximum and label follow. The minimum is always 0.
        float ParamDefault = 0.0f;
        float ParamMax = 1.0f;
        const char* ParamLabel = "Param"; // ImGui label
        bool bHasLoop = false;            // ClipPlayer: looping is a node property, never the clip's
    };

    inline const std::vector<NodeTypeDesc>& NodeTypeRegistry()
    {
        static const std::vector<NodeTypeDesc> R = {
                //  type              inputs                                                   outputs                       clip   param  cache  GPU
                {"Root", {{"Result", PinType::Pose}}, {}, false, false, false, true},
                // ClipPlayer: Param is the play rate (1 = authored speed, 0 = frozen); stored/run as PlayRate.
                {"ClipPlayer", {}, {{"Pose", PinType::Pose}}, true, true, false, true, 1.0f, 3.0f, "Rate", true},
                {"TwoWayBlend",
                 {{"A", PinType::Pose}, {"B", PinType::Pose}},
                 {{"Pose", PinType::Pose}},
                 false, true, false, true, 0.5f, 1.0f, "Alpha"},
                {"BlendSpace1D",
                 {{"Sample0", PinType::Pose}, {"Sample1", PinType::Pose}, {"Sample2", PinType::Pose}},
                 {{"Pose", PinType::Pose}},
                 false, true, false, true, 0.5f, 1.0f, "Input"},
                {"SaveCachedPose", {{"Source", PinType::Pose}}, {}, false, false, true, false},
                {"UseCachedPose", {}, {{"Pose", PinType::Pose}}, false, false, true, false},

                // CpuOnly: a placement marker that passes the pose through unchanged and exists only to force
                // the CPU side. Placement is otherwise inferred (GPU-capable nodes go to the GPU); to keep a
                // GPU-capable subgraph on the CPU, insert a CpuOnly downstream of it - the CPU property
                // propagates to the whole upstream.
                //   - in front of Root: the whole graph is forced to the CPU (baseline comparison)
                //   - on one branch: an explicit seam (that edge becomes an upload slot)
                // It does not exist at runtime: FoldCpuOnly rewires consumers to its Source and flags the Source.
                {"CpuOnly", {{"Source", PinType::Pose}}, {{"Pose", PinType::Pose}}, false, false, false, false},
        };
        return R;
    }

    inline const NodeTypeDesc* FindNodeType(const std::string& typeName)
    {
        for (const NodeTypeDesc& d : NodeTypeRegistry())
        {
            if (d.TypeName == typeName)
            {
                return &d;
            }
        }
        return nullptr;
    }
} // namespace RAnimation
