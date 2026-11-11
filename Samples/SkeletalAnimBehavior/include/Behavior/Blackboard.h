#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

#include <Model/EntityId.h>

// Typed, schema-driven blackboard (D11). Four scopes; every store is a flat array addressed by slot, so
// node parameters bound to "@key" are compiled to (scope, slot) once and never look names up at runtime.

namespace RAnimation
{
    enum class BbType : uint8_t
    {
        Bool,
        Int,
        Float,
        Vec3,
        String,
        Entity
    };

    enum class BbScope : uint8_t
    {
        Global,     // one store for the whole world (behavior/global.blackboard.usda)
        TreeShared, // one store per tree asset, shared by every agent running it
        Instance,   // one store per agent: the perception.* keys + the tree's instance keys
        Node        // per agent, per node: only that node and the decorators directly above it
    };

    constexpr uint32_t kNoOwnerNode = ~0u;

    // Variant order is the BbType order.
    using BbValue = std::variant<bool, int, float, glm::vec3, std::string, EntityId>;

    const char* BbTypeName(BbType type);
    bool ParseBbType(const std::string& name, BbType& out);
    const char* BbScopeName(BbScope scope);
    bool ParseBbScope(const std::string& name, BbScope& out);

    BbType TypeOf(const BbValue& value);
    BbValue DefaultValueFor(BbType type);
    // bool <-> int <-> float promotions; false when the types are unrelated (vec3 vs string ...).
    bool ConvertValue(const BbValue& in, BbType to, BbValue& out);
    // Equality used for change detection: vec3 with tolerance 1e-4, strings by content.
    bool ValuesEqual(const BbValue& a, const BbValue& b);
    std::string ValueToString(const BbValue& value);
    // Literal parsing for the editor / file formats ("1.5", "true", "1, 0, 2", "#17").
    bool ValueFromString(BbType type, const std::string& text, BbValue& out);
    float ValueAsFloat(const BbValue& value);

    struct BlackboardKeyDecl
    {
        std::string name;
        BbType type = BbType::Float;
        BbScope scope = BbScope::Instance;
        BbValue defaultValue = 0.0f;
        bool wakeOnChange = false;         // D9: a change wakes the agents that use the key
        uint32_t ownerNode = kNoOwnerNode; // Node scope only: node id inside the tree asset
        bool builtin = false;              // read-only runtime data shown in the inspector (D12)
    };

    struct BlackboardSchema
    {
        std::vector<BlackboardKeyDecl> keys;

        int Find(const std::string& name) const
        {
            for (size_t i = 0; i < keys.size(); ++i)
            {
                if (keys[i].name == name)
                {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }
    };

    // One scope's values. Owns a copy of its declarations, so a store survives the schema it was built from.
    class BlackboardStore
    {
    public:
        void Init(const std::vector<BlackboardKeyDecl>& decls);
        void Init(const BlackboardSchema& schema) { Init(schema.keys); }
        void ResetToDefaults();

        size_t Size() const { return mValues.size(); }
        const BlackboardKeyDecl& Decl(uint32_t slot) const { return mDecls[slot]; }
        const std::vector<BlackboardKeyDecl>& Decls() const { return mDecls; }
        int Find(const std::string& name) const;

        const BbValue& Get(uint32_t slot) const { return mValues[slot]; }
        // Entity keys additionally need a non-zero id to count as set.
        bool IsSet(uint32_t slot) const;

        // Converts to the declared type; true when the value (or its set flag) changed. A change of a
        // wakeOnChange key calls onChange(slot, writer) - the owner routes it to the agents to wake, skipping
        // the writer itself (a tree's own writes never wake it, D10).
        bool Set(uint32_t slot, const BbValue& value, EntityId writer);
        bool Clear(uint32_t slot, EntityId writer);
        // Set by name (perception, tests); false if the key does not exist.
        bool SetByName(const std::string& name, const BbValue& value, EntityId writer);

        std::function<void(uint32_t slot, EntityId writer)> onChange;

    private:
        std::vector<BlackboardKeyDecl> mDecls;
        std::vector<BbValue> mValues;
        std::vector<uint8_t> mSet;
    };

    using Blackboard = BlackboardStore;
} // namespace RAnimation
