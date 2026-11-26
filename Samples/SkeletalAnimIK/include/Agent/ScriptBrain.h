#pragma once
#include <functional>

#include <Agent/IDecisionMaker.h>

namespace RAnimation
{
    // A frame-indexed script as a decision maker: the --controller smoke test (and the scripted player of
    // --behavior) drive their character through the same interface as a behaviour tree. Ticks every frame.
    class ScriptBrain final : public IDecisionMaker
    {
    public:
        using Script = std::function<void(int frame, DecisionContext& ctx, AgentIntent& intent)>;

        explicit ScriptBrain(Script script) : mScript(std::move(script)) {}

        // The --controller sequence (walk, jog, jump, punch, crouch, stand, draw, hit, die).
        static Script ControllerScript();

        void Tick(DecisionContext& ctx, AgentIntent& intent) override
        {
            if (mScript)
            {
                mScript(mFrame, ctx, intent);
            }
            ++mFrame;
        }
        void Halt(AgentIntent& intent) override
        {
            intent.direct.reset();
            intent.ResetChannels();
        }
        void Reset() override { mFrame = 0; }
        bool SaveRuntime(SaveNode& out) const override
        {
            out.Set("frame", mFrame);
            return true;
        }
        bool LoadRuntime(const SaveNode& in, std::string&) override
        {
            in.Get("frame", mFrame);
            return true;
        }
        const char* Kind() const override { return "Script"; }
        int Frame() const { return mFrame; }

    private:
        Script mScript;
        int mFrame = 0;
    };
} // namespace RAnimation
