#pragma once
#include <vector>

#include <Agent/IDecisionMaker.h>

namespace RAnimation
{
    // Filled by the application from the keyboard every frame (Application::updateCharacterInput).
    struct PlayerInputBuffer
    {
        CharacterInput input;
        std::vector<IntentCommand> commands; // drained by PlayerBrain::Tick
    };

    // The possessed agent's decision source (D3): passes the keyboard straight through (intent.direct).
    class PlayerBrain final : public IDecisionMaker
    {
    public:
        explicit PlayerBrain(PlayerInputBuffer* buffer) : mBuffer(buffer) {}

        void Tick(DecisionContext&, AgentIntent& intent) override
        {
            intent.direct = mBuffer != nullptr ? mBuffer->input : CharacterInput{};
            if (mBuffer != nullptr)
            {
                for (IntentCommand& c : mBuffer->commands)
                {
                    intent.commands.push_back(std::move(c));
                }
                mBuffer->commands.clear();
            }
        }
        void Halt(AgentIntent& intent) override
        {
            intent.direct.reset();
            intent.ResetChannels();
        }
        void Reset() override {}
        bool SaveRuntime(SaveNode&) const override { return true; }
        bool LoadRuntime(const SaveNode&, std::string&) override { return true; }
        const char* Kind() const override { return "Player"; }

    private:
        PlayerInputBuffer* mBuffer = nullptr;
    };
} // namespace RAnimation
