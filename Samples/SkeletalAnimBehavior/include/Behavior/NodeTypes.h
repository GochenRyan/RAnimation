#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <Behavior/BehaviorTreeAsset.h>
#include <Behavior/Blackboard.h>

// Node types (§5.3-5.5): stateless functions over a TreeContext. All runtime state lives in the agent's
// BehaviorTreeInstance, so one compiled tree serves every agent (the book's "clone the whole tree per
// instance" limitation goes away).

namespace RAnimation
{
    class TreeContext;

    enum class NodeCategory : uint8_t
    {
        Composite,
        Decorator,
        Leaf
    };
    const char* NodeCategoryName(NodeCategory category);

    enum class ParamKind : uint8_t
    {
        Value, // literal, or bound to a key of a compatible type
        Key,   // must be bound (the node reads / writes that key)
        Enum   // string literal from `options`
    };

    struct ParamSpec
    {
        std::string name;
        BbType type = BbType::Float;
        ParamKind kind = ParamKind::Value;
        BbValue defaultValue = 0.0f;
        std::vector<std::string> options; // Enum
        std::vector<BbType> keyTypes;     // Key: accepted key types (empty = any)
        int sameTypeAs = -1;              // Value whose type follows another (Key) parameter
        std::string doc;
    };

    class NodeType
    {
    public:
        std::string name;
        NodeCategory category = NodeCategory::Leaf;
        std::vector<ParamSpec> params;
        std::vector<std::string> builtins; // read-only runtime data shown as node keys (D12)
        std::string doc;
        // Leaves: whether the node runs across ticks; may depend on parameters (PlayAction). Composites /
        // decorators are async when any child is, plus selfAsync ones (Repeat(-1), Timeout, Cooldown).
        std::function<bool(const CompiledNode&)> async;
        std::function<bool(const CompiledNode&)> selfAsync;
        std::function<NodeStatus(TreeContext&, uint32_t)> tick;
        std::function<void(TreeContext&, uint32_t)> halt; // leaf cleanup (channels, CancelAction)

        int ParamIndex(const std::string& paramName) const;
        int MinChildren() const { return category == NodeCategory::Leaf ? 0 : 1; }
        int MaxChildren() const { return category == NodeCategory::Composite ? 64 : (category == NodeCategory::Decorator ? 1 : 0); }
    };

    class NodeTypeRegistry
    {
    public:
        NodeTypeRegistry(); // registers the built-in set
        const NodeType* Find(const std::string& name) const;
        const std::vector<std::unique_ptr<NodeType>>& All() const { return mTypes; }
        NodeType& Add(std::unique_ptr<NodeType> type);

    private:
        std::vector<std::unique_ptr<NodeType>> mTypes;
    };

    // Comparison operators of BlackboardCompare / BlackboardCondition.
    const std::vector<std::string>& CompareOps();
    bool EvaluateCompare(const std::string& op, const BbValue& left, bool leftSet, const BbValue& right);
} // namespace RAnimation
