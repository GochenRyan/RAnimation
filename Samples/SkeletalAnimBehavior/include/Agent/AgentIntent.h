#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include <Agent/CharacterSignals.h>
#include <Agent/ExecutionFeedback.h>
#include <Model/EntityId.h>

// The only output of a decision maker (D2). Goal level ("walk to X", "face Y") and declarative ("want to be
// armed"); Steering turns it into CharacterInput + controller calls every frame.

namespace RAnimation
{
    constexpr uint32_t kNoOwner = ~0u;

    enum class Gait : uint8_t
    {
        Walk,
        Jog,
        Sprint
    };

    enum class LocomotionMode : uint8_t
    {
        None,
        MoveTo,
        MoveDirection,
        FaceTo
    };

    struct LocomotionIntent // latched between ticks
    {
        LocomotionMode mode = LocomotionMode::None;
        glm::vec3 target = glm::vec3(0.0f);    // MoveTo / FaceTo, world
        EntityId targetEntity = kInvalidEntityId; // != 0: follow this entity's position (wins over target)
        glm::vec3 direction = glm::vec3(0.0f); // MoveDirection, world horizontal
        Gait gait = Gait::Walk;
        bool crouch = false;
        float arriveRadius = 0.3f;
    };

    struct StanceIntent // latched
    {
        bool aim = false;
        float aimPitch = 0.0f;
        bool wantArmed = false; // declarative: Steering sends ToggleWeapon until feedback.armed matches
    };

    enum class CommandType : uint8_t
    {
        Jump,
        Action,
        CancelAction,
        Die,
        ToggleWeapon, // direct (player / script) only; decision makers use StanceIntent::wantArmed
        Interact      // player only: the U-key interaction request (perception, not the state machine)
    };

    struct IntentCommand // one-shot: consumed by Steering in the frame it is issued
    {
        CommandType type = CommandType::Action;
        RequestId id = 0;           // Action: allocated by the agent, looked up in the feedback
        std::string montage;        // Action
        RequestId cancelTarget = 0; // CancelAction: the request to cancel, 0 = whatever is playing
    };

    struct AgentIntent
    {
        LocomotionIntent locomotion;
        uint32_t locomotionOwner = kNoOwner; // D14: node index that wrote the channel last
        StanceIntent stance;
        uint32_t stanceOwner = kNoOwner;
        std::vector<IntentCommand> commands;
        std::optional<CharacterInput> direct; // player / script: passed through, locomotion / stance ignored

        void ResetChannels()
        {
            locomotion = LocomotionIntent{};
            locomotionOwner = kNoOwner;
            stance = StanceIntent{};
            stanceOwner = kNoOwner;
        }
    };

    // Hands out request ids for one agent (monotonic; persisted in saves so ids never repeat).
    class AgentRequestAllocator
    {
    public:
        RequestId Allocate() { return mNext++; }
        RequestId Peek() const { return mNext; }
        void Restore(RequestId next) { mNext = next > 0 ? next : 1; }

    private:
        RequestId mNext = 1;
    };
} // namespace RAnimation
