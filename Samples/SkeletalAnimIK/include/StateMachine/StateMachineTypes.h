#pragma once
#include <any>
#include <cstdint>
#include <map>
#include <string>
#include <type_traits>
#include <variant>

// Shared vocabulary of the state-machine module (FSM / HFSM / parallel regions). Everything here is
// plain data: ids are indices into the owning StateMachineDefinition, events carry an optional payload.

namespace RAnimation
{
    using StateId = uint32_t;
    using TransitionId = uint32_t;
    using EventId = uint32_t;

    constexpr StateId kInvalidStateId = ~0u;
    constexpr TransitionId kInvalidTransitionId = ~0u;
    constexpr EventId kInvalidEventId = ~0u;

    enum class StateKind
    {
        Leaf,    // no children
        Single,  // XOR: exactly one child is active
        Parallel // AND: every child (region) is active
    };

    // When a ParallelState counts as "done" (IsDone) for completion transitions.
    enum class CompletionPolicy
    {
        All, // every region is done
        Any  // at least one region is done
    };

    const char* StateKindName(StateKind kind);
    bool ParseStateKind(const std::string& name, StateKind& out);
    const char* CompletionPolicyName(CompletionPolicy policy);
    bool ParseCompletionPolicy(const std::string& name, CompletionPolicy& out);

    // An event delivered to a manager: a registered id plus an optional, untyped payload. The payload is
    // not serialized; states that need durable data keep it in their IStateInstanceData.
    struct Event
    {
        EventId id = kInvalidEventId;
        std::any payload;
    };

    // Typed key/value bag. Used for the parameters authored on a state in the definition (passed to the
    // state factory) and for the runtime data a state chooses to serialize. Kept tiny on purpose: four
    // value types, all representable as USD attributes.
    class ParamBag
    {
    public:
        using Value = std::variant<bool, int, float, std::string>;

        bool Has(const std::string& key) const { return mValues.count(key) != 0; }

        void Set(const std::string& key, Value value) { mValues[key] = std::move(value); }

        template <typename T> T Get(const std::string& key, const T& fallback) const
        {
            auto it = mValues.find(key);
            if (it == mValues.end())
            {
                return fallback;
            }
            if (const T* v = std::get_if<T>(&it->second))
            {
                return *v;
            }
            // Allow int -> float promotion (authored "1" for a float parameter).
            if constexpr (std::is_same_v<T, float>)
            {
                if (const int* i = std::get_if<int>(&it->second))
                {
                    return static_cast<float>(*i);
                }
            }
            return fallback;
        }

        const std::map<std::string, Value>& Values() const { return mValues; }

        bool operator==(const ParamBag& other) const { return mValues == other.mValues; }

    private:
        std::map<std::string, Value> mValues;
    };
} // namespace RAnimation
