#include <Editor/Editor.h>

using namespace RAnimation;

Editor::~Editor()
{
    Clear();
}

void Editor::Execute(std::unique_ptr<ICommand> command)
{
    if (!command)
    {
        return;
    }

    command->Do();
    Record(std::move(command));
}

void Editor::Record(std::unique_ptr<ICommand> command)
{
    if (!command)
    {
        return;
    }

    clearRedo();

    mUndoStack.emplace_back(std::move(command));

    // Trim from the oldest end. Those commands stay applied, so signal currentlyApplied = true.
    while (mUndoStack.size() > kMaxHistory)
    {
        mUndoStack.front()->OnDiscard(true);
        mUndoStack.erase(mUndoStack.begin());
    }
}

const char* Editor::UndoName() const
{
    return CanUndo() ? mUndoStack.back()->Name() : "";
}

const char* Editor::RedoName() const
{
    return CanRedo() ? mRedoStack.back()->Name() : "";
}

void Editor::Undo()
{
    if (!CanUndo())
    {
        return;
    }

    std::unique_ptr<ICommand> command = std::move(mUndoStack.back());
    mUndoStack.pop_back();
    command->Undo();
    mRedoStack.emplace_back(std::move(command));
}

void Editor::Redo()
{
    if (!CanRedo())
    {
        return;
    }

    std::unique_ptr<ICommand> command = std::move(mRedoStack.back());
    mRedoStack.pop_back();
    command->Do();
    mUndoStack.emplace_back(std::move(command));
}

void Editor::Clear()
{
    // Undo entries are applied; redo entries are reverted.
    for (auto& command : mUndoStack)
    {
        command->OnDiscard(true);
    }
    mUndoStack.clear();

    clearRedo();
}

void Editor::clearRedo()
{
    for (auto& command : mRedoStack)
    {
        command->OnDiscard(false);
    }
    mRedoStack.clear();
}
