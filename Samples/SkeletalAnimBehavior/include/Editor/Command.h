#pragma once

#include <functional>
#include <string>

namespace RAnimation
{
    // A single undoable operation. Do() applies the effect, Undo() reverts it. The pair must be
    // reversible any number of times so a command can ride the undo/redo stacks repeatedly.
    class ICommand
    {
    public:
        virtual ~ICommand() = default;

        virtual void Do() = 0;
        virtual void Undo() = 0;

        // Label shown in the Edit menu ("Undo <Name>").
        virtual const char* Name() const = 0;

        // Called when the command is permanently dropped from history (redo stack cleared by a new
        // action, history trimmed, or the editor torn down). `currentlyApplied` is true when the
        // command's effect is still live (it sat on the undo stack), false when it was reverted (it
        // sat on the redo stack). Commands that own deferred resources (e.g. a deleted model's GPU
        // memory) use this to release them exactly once, in the right state.
        virtual void OnDiscard(bool currentlyApplied)
        {
            (void) currentlyApplied;
        }
    };

    // Command backed by std::function lambdas. Lets call sites that own the real state (the Renderer)
    // keep all type-specific logic local via captures, instead of leaking internals into the editor.
    class FunctionalCommand final : public ICommand
    {
    public:
        FunctionalCommand(std::string name,
                          std::function<void()> doFn,
                          std::function<void()> undoFn,
                          std::function<void(bool)> onDiscardFn = {})
            : mName(std::move(name)),
              mDo(std::move(doFn)),
              mUndo(std::move(undoFn)),
              mOnDiscard(std::move(onDiscardFn))
        {
        }

        void Do() override
        {
            if (mDo)
            {
                mDo();
            }
        }

        void Undo() override
        {
            if (mUndo)
            {
                mUndo();
            }
        }

        const char* Name() const override
        {
            return mName.c_str();
        }

        void OnDiscard(bool currentlyApplied) override
        {
            if (mOnDiscard)
            {
                mOnDiscard(currentlyApplied);
            }
        }

    private:
        std::string mName;
        std::function<void()> mDo;
        std::function<void()> mUndo;
        std::function<void(bool)> mOnDiscard;
    };
} // namespace RAnimation
