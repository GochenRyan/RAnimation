#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include <Behavior/Blackboard.h>

// Behaviour tree asset (D13): TreeDesc is the authored / editable form (.behaviortree.usda, the editor),
// CompiledTree the immutable runtime form every agent of the tree shares (nodes in depth-first order,
// parameters bound to (scope, slot), sync / async derived, validated).

namespace RAnimation
{
    class NodeType;
    class NodeTypeRegistry;

    enum class NodeStatus : uint8_t
    {
        Idle, // never ran, or halted / reset (debug display only)
        Running,
        Success,
        Failure
    };
    const char* NodeStatusName(NodeStatus status);
    bool ParseNodeStatus(const std::string& name, NodeStatus& out);

    // One parameter as authored: a literal, or "@key" bound to a blackboard key.
    struct ParamDesc
    {
        std::string name;
        bool bound = false;
        std::string key;  // bound
        BbValue literal;  // !bound
    };

    struct BtNodeDesc
    {
        uint32_t id = 0;                // stable inside the asset (saves locate nodes by it), never reused
        std::string type;               // NodeTypeRegistry name
        std::string label;              // optional display name
        std::vector<uint32_t> children; // ordered (the editor sorts them by x)
        std::vector<ParamDesc> params;
        std::vector<BlackboardKeyDecl> nodeKeys; // Node scope (D12), ownerNode = id
        glm::vec2 editorPos = glm::vec2(0.0f);

        const ParamDesc* FindParam(const std::string& paramName) const;
        ParamDesc& Param(const std::string& paramName); // creates when missing
    };

    struct TreeDesc
    {
        std::string name;
        std::string doc;
        float tickHz = 10.0f; // 0 = every frame
        uint32_t rootId = 0;
        uint32_t nextId = 1;
        std::vector<BtNodeDesc> nodes;
        // TreeShared / Instance keys declared by the tree, plus Global keys it references (name + type,
        // checked against the global schema). Instance keys may redeclare a perception key (same type) to
        // change its wake flag.
        std::vector<BlackboardKeyDecl> keys;

        BtNodeDesc* Find(uint32_t id);
        const BtNodeDesc* Find(uint32_t id) const;
    };

    struct ValidationError
    {
        uint32_t nodeId = ~0u; // ~0u: tree level
        std::string message;
    };

    struct CompiledParam
    {
        bool bound = false;
        BbScope scope = BbScope::Instance;
        uint32_t slot = 0;
        uint32_t ownerIndex = 0; // Node scope: index of the node that declares the key
        BbType keyType = BbType::Float;
        BbValue literal;
    };

    struct CompiledNode
    {
        uint32_t id = 0;
        const NodeType* type = nullptr;
        uint32_t parent = ~0u; // index
        std::vector<uint32_t> children; // indices
        std::vector<CompiledParam> params; // in NodeType::params order
        bool async = false;
        std::string label;
        std::string path; // "Root/1:MemSequence/0:MoveTo" for logs and errors
    };

    struct CompiledTree
    {
        std::string path; // asset path relative to Assets/SkeletalAnimBehavior ("" for in-memory trees)
        std::shared_ptr<const TreeDesc> desc;
        uint64_t contentHash = 0; // structure + parameters (not editor positions); saves compare it
        float tickHz = 10.0f;
        std::vector<CompiledNode> nodes; // depth-first; nodes[0] = root
        std::unordered_map<uint32_t, uint32_t> indexById;
        BlackboardSchema instanceSchema;   // PerceptionSchema() first (with overrides), then the tree's keys
        BlackboardSchema treeSharedSchema;
        std::vector<std::vector<BlackboardKeyDecl>> nodeKeys; // per node index (user keys)
        std::vector<uint32_t> globalSlotsUsed;                // global store slots the tree reads / writes

        int IndexOf(uint32_t id) const
        {
            const auto it = indexById.find(id);
            return it != indexById.end() ? static_cast<int>(it->second) : -1;
        }
    };

    // Validates (§9.2 rules 1-7) and compiles. Returns null and fills errors when the tree is invalid.
    std::shared_ptr<const CompiledTree> CompileTree(std::shared_ptr<const TreeDesc> desc,
                                                    const std::string& path,
                                                    const NodeTypeRegistry& types,
                                                    const BlackboardSchema& globalSchema,
                                                    std::vector<ValidationError>& errors);

    // Structure + parameters + keys, independent of editor positions and doc text.
    uint64_t ComputeContentHash(const TreeDesc& desc);

    // Tidy top-down layout from the root (leaves spread on x in child order, parents centred above their
    // children); nodes not reachable from the root are stacked on the right. Child order is preserved, so a
    // later sort by x keeps it.
    void AutoLayoutTree(TreeDesc& desc, float spacingX = 320.0f, float spacingY = 170.0f);
} // namespace RAnimation
