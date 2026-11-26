#pragma once

#include <memory>
#include <vector>

#include <Editor/Command.h>

namespace RAnimation
{
    // Holds the editor mode (View vs. Edit) and the undo/redo history. Generic: it only ever talks
    // to ICommand, so it has no dependency on Renderer, Model, or any GPU type.
    class Editor final
    {
    public:
        enum class Mode
        {
            View,
            Edit
        };

        ~Editor();

        Mode GetMode() const
        {
            return mMode;
        }
        bool IsEditMode() const
        {
            return mMode == Mode::Edit;
        }
        void SetMode(Mode mode)
        {
            mMode = mode;
        }
        void ToggleMode()
        {
            mMode = IsEditMode() ? Mode::View : Mode::Edit;
        }

        // Run a fresh command: applies it (Do) then records it. Use for structural operations.
        void Execute(std::unique_ptr<ICommand> command);

        // Record a command whose effect was already applied live this frame (e.g. an ImGui slider
        // drag). Pushed onto the undo stack without re-running Do.
        void Record(std::unique_ptr<ICommand> command);

        bool CanUndo() const
        {
            return !mUndoStack.empty();
        }
        bool CanRedo() const
        {
            return !mRedoStack.empty();
        }

        const char* UndoName() const;
        const char* RedoName() const;

        void Undo();
        void Redo();

        // Discard all history (used at shutdown so deferred resources release while the device lives).
        void Clear();

    private:
        // Drops the redo stack, notifying each entry it was discarded in the reverted state.
        void clearRedo();

        Mode mMode = Mode::Edit;

        std::vector<std::unique_ptr<ICommand>> mUndoStack;
        std::vector<std::unique_ptr<ICommand>> mRedoStack;

        // Cap history so long-lived sessions cannot grow without bound; oldest applied entries drop.
        static constexpr size_t kMaxHistory = 256;
    };
} // namespace RAnimation
