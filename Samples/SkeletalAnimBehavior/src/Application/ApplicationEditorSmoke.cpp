// --editor-smoke: the behaviour tree editor driven through ImGui with injected mouse / key events (the §12.3
// manual checklist, automated). Every step is a real interaction with the node editor or a toolbar widget; the
// checks read the panel / scene state afterwards. Needs --scene behavior_test.level.usda (agent 13 runs the test
// tree so Edit -> View can show that exactly the agents of a modified tree start over).

#include <Application/Application.h>

#include <deque>
#include <filesystem>
#include <functional>

#include <fmt/base.h>
#include <fmt/color.h>
#include <fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdlib>

#include <Behavior/BehaviorTreeBrain.h>

using namespace RAnimation;

struct Application::EditorSmoke
{
    struct Op
    {
        std::string name;
        std::function<void(ImGuiIO&)> act;
        int hold = 1; // frames to wait after the action
    };
    std::deque<Op> ops;
    int holdLeft = 0;
    bool ok = true;
    bool finished = false;
    int checks = 0;
    std::string treePath = "behavior/_editor_smoke.behaviortree.usda";
    ImVec2 dragFrom, dragTo;
    const IDecisionMaker* requesterBrainBefore = nullptr;
    const IDecisionMaker* talkerBrainBefore = nullptr;
    uint64_t savedHash = 0;
    uint32_t addedNode = 0;
    // virtual mouse: the SDL backend re-sends the real global mouse position every frame, so the injected
    // position is re-sent every frame too (after the backend, so it wins)
    ImVec2 mouse = ImVec2(-1.0f, -1.0f);
    bool mouseActive = false;
};

namespace
{
    constexpr EntityId kRequester = 13;
    constexpr EntityId kTalker = 12;
} // namespace

bool Application::setupEditorSmoke()
{
    auto s = std::make_shared<EditorSmoke>();
    mEditorSmoke = s;
    ModelAndInstanceData& scene = mSceneEditor.ModelData();
    BehaviorTreePanel& panel = mUserInterface.BehaviorTrees();

    // test tree: MemSequence root -> Log a, Log b (valid)
    TreeDesc d;
    d.name = "editor smoke";
    d.tickHz = 10.0f;
    auto add = [&d](const char* type, glm::vec2 pos, std::vector<uint32_t> children = {})
    {
        BtNodeDesc n;
        n.id = d.nextId++;
        n.type = type;
        n.editorPos = pos;
        n.children = std::move(children);
        if (std::string(type) == "Log")
        {
            n.params.push_back(ParamDesc{"text", false, {}, std::string(n.id == 2 ? "a" : "b")});
        }
        d.nodes.push_back(n);
        return d.nodes.back().id;
    };
    const uint32_t root = d.nextId; // 1
    add("MemSequence", {150.0f, 0.0f}, {2, 3});
    add("Log", {0.0f, 160.0f});
    add("Log", {300.0f, 160.0f});
    d.rootId = root;
    std::string error;
    if (!WriteTreeDesc(d, mBehaviorSystem.Types(), mBehaviorSystem.AbsolutePath(s->treePath), error))
    {
        fmt::print(stderr, "editor smoke: {}\n", error);
        return false;
    }
    // agent 13 runs it (its brain must be rebuilt by Edit -> View), agent 12 does not
    mCharacterSystem.SyncFromScene(scene);
    std::shared_ptr<ModelInstance> requester;
    for (const auto& instance : scene.miModelInstances)
    {
        if (instance->GetEntityId() == kRequester)
        {
            requester = instance;
            AgentConfig c = *instance->GetAgentConfig();
            c.tree = s->treePath;
            instance->SetAgentConfig(c);
        }
    }
    if (!requester || mCharacterSystem.Find(kTalker) == nullptr)
    {
        return false;
    }
    mCharacterSystem.SyncFromScene(scene);
    mSceneEditor.SetMode(Editor::Mode::Edit);
    panel.bVisible = true;
    panel.LoadTree(s->treePath);
    if (mFixedDeltaTime <= 0.0f)
        mFixedDeltaTime = 1.0f / 60.0f;
    mFrameLimit = 4000; // the script ends the run when it is done

    // ---- script helpers ----
    auto pos = [this](std::string what) -> std::function<ImVec2()>
    {
        return [this, what]()
        {
            float x = -1.0f, y = -1.0f;
            if (!mUserInterface.BehaviorTrees().ScreenPos(what, x, y))
            {
                fmt::print(fg(fmt::color::red), "editor smoke: '{}' is not on screen\n", what);
            }
            return ImVec2(x, y);
        };
    };
    auto wait = [s](int frames) { s->ops.push_back({"wait", [](ImGuiIO&) {}, frames}); };
    auto click = [s](std::function<ImVec2()> at)
    {
        auto target = std::make_shared<ImVec2>();
        s->ops.push_back({"move", [s, at, target](ImGuiIO&)
                          {
                              *target = at();
                              s->mouse = *target;
                              s->mouseActive = true;
                          },
                          2});
        s->ops.push_back({"press", [](ImGuiIO& io) { io.AddMouseButtonEvent(0, true); }, 2});
        s->ops.push_back({"release", [](ImGuiIO& io) { io.AddMouseButtonEvent(0, false); }, 4});
    };
    auto drag = [s](std::function<ImVec2()> from, std::function<ImVec2(ImVec2)> to)
    {
        s->ops.push_back({"drag start", [s, from, to](ImGuiIO& io)
                          {
                              s->dragFrom = from();
                              s->dragTo = to(s->dragFrom);
                              s->mouse = s->dragFrom;
                              s->mouseActive = true;
                          },
                          2});
        s->ops.push_back({"drag press", [](ImGuiIO& io) { io.AddMouseButtonEvent(0, true); }, 2});
        for (int i = 1; i <= 16; ++i)
        {
            const float t = static_cast<float>(i) / 16.0f;
            s->ops.push_back({"drag move", [s, t](ImGuiIO& io)
                              { s->mouse = ImVec2(s->dragFrom.x + (s->dragTo.x - s->dragFrom.x) * t, s->dragFrom.y + (s->dragTo.y - s->dragFrom.y) * t); },
                              1});
        }
        s->ops.push_back({"drag release", [](ImGuiIO& io) { io.AddMouseButtonEvent(0, false); }, 6});
    };
    auto key = [s](ImGuiKey k, bool ctrl)
    {
        s->ops.push_back({"key down", [k, ctrl](ImGuiIO& io)
                          {
                              if (ctrl)
                                  io.AddKeyEvent(ImGuiMod_Ctrl, true);
                              io.AddKeyEvent(k, true);
                          },
                          1});
        s->ops.push_back({"key up", [k, ctrl](ImGuiIO& io)
                          {
                              io.AddKeyEvent(k, false);
                              if (ctrl)
                                  io.AddKeyEvent(ImGuiMod_Ctrl, false);
                          },
                          4});
    };
    auto check = [s](std::string what, std::function<bool()> condition)
    {
        s->ops.push_back({"check", [s, what, condition](ImGuiIO&)
                          {
                              const bool r = condition();
                              ++s->checks;
                              s->ok = s->ok && r;
                              fmt::print(r ? fmt::text_style{} : fg(fmt::color::red), "smoke[editor] {} {}\n", r ? "ok  " : "FAIL", what);
                          },
                          1});
    };
    auto children = [&panel](uint32_t id)
    {
        const BtNodeDesc* n = panel.Desc().Find(id);
        return n != nullptr ? n->children : std::vector<uint32_t>{};
    };
    auto order = [children](uint32_t id)
    {
        std::string text;
        for (uint32_t c : children(id))
            text += std::to_string(c) + " ";
        return text;
    };

    // ---- the checklist ----
    s->ops.push_back({"front", [this](ImGuiIO&) { mUserInterface.BehaviorTrees().BringToFront(); }, 30});
    check("the editor shows the test tree, valid, nodes on screen",
          [&panel, pos]() { return panel.CurrentPath() == "behavior/_editor_smoke.behaviortree.usda" && panel.Errors().empty() && pos("title:2")().x > 0.0f; });

    // add a node with the toolbar (default type: ReactiveSequence, an empty composite and an orphan -> invalid)
    click(pos("Add node"));
    wait(5);
    check("Add node: a new node, the tree is now invalid (orphan)", [s, &panel]()
          {
              s->addedNode = panel.Desc().nextId - 1;
              return panel.Desc().nodes.size() == 4 && !panel.Errors().empty() && panel.Dirty();
          });
    click(pos("Save"));
    check("Save refuses an invalid tree (file unchanged)", [this, &panel, s]()
          {
              TreeDesc onDisk;
              std::string e;
              ReadTreeDesc(mBehaviorSystem.AbsolutePath(s->treePath), onDisk, e);
              return panel.Status().find("not saved") != std::string::npos && onDisk.nodes.size() == 3;
          });
    key(ImGuiKey_E, true);
    check("Ctrl+E (Edit -> View) is refused while the tree is unsaved / invalid", [this]() { return mSceneEditor.IsEditMode(); });
    click(pos("tab:Errors"));
    wait(3);
    click(pos("error0"));
    wait(5);
    check("clicking an error focuses its node", [s, &panel]() { return panel.SelectedNode() == s->addedNode; });

    s->ops.push_back({"frame all", [this](ImGuiIO&) { mUserInterface.BehaviorTrees().FrameAll(); }, 6});
    // link root -> new node (accepted), new node -> node 3 (rejected: 3 already has a parent)
    drag(pos("out:1"), [this, s](ImVec2) { float x, y; mUserInterface.BehaviorTrees().ScreenPos("in:" + std::to_string(s->addedNode), x, y); return ImVec2(x, y); });
    check("link root -> new node: accepted, children ordered by x", [s, children]() { auto c = children(1); return c.size() == 3 && c.back() == s->addedNode; });
    drag([this, s]() { float x, y; mUserInterface.BehaviorTrees().ScreenPos("out:" + std::to_string(s->addedNode), x, y); return ImVec2(x, y); },
         [this](ImVec2) { float x, y; mUserInterface.BehaviorTrees().ScreenPos("in:3", x, y); return ImVec2(x, y); });
    check("link to a node that already has a parent: rejected with a reason", [s, &panel, children]()
          { return children(s->addedNode).empty() && panel.Status().find("already has a parent") != std::string::npos; });

    // delete the new node (select + Delete), undo, redo
    click([this, s]() { float x, y; mUserInterface.BehaviorTrees().ScreenPos("title:" + std::to_string(s->addedNode), x, y); return ImVec2(x, y); });
    key(ImGuiKey_Delete, false);
    check("Delete removes the selected node and its link; the tree is valid again", [s, &panel, children]()
          { return panel.Desc().Find(s->addedNode) == nullptr && children(1).size() == 2 && panel.Errors().empty(); });
    key(ImGuiKey_Z, true);
    check("Ctrl+Z brings the node and its link back", [s, &panel, children]() { return panel.Desc().Find(s->addedNode) != nullptr && children(1).size() == 3; });
    key(ImGuiKey_Y, true);
    check("Ctrl+Y deletes it again", [s, &panel]() { return panel.Desc().Find(s->addedNode) == nullptr; });

    // reorder: drag node 2 to the right of node 3 -> children follow x (D17)
    check("before the drag the order is 2 3", [order]() { return order(1) == "2 3 "; });
    drag(pos("title:2"), [](ImVec2 from) { return ImVec2(from.x + 420.0f, from.y); });
    wait(4);
    check("dragging node 2 past node 3 reorders the children (3 2) when the mouse is released", [order]() { return order(1) == "3 2 "; });
    key(ImGuiKey_Z, true);
    wait(2);
    check("Ctrl+Z undoes the reorder (2 3)", [order]() { return order(1) == "2 3 "; });
    key(ImGuiKey_Y, true);
    wait(2);
    check("Ctrl+Y redoes it (3 2)", [order]() { return order(1) == "3 2 "; });

    // save, then Edit -> View: exactly the agents of the modified tree start over
    click(pos("Save"));
    check("Save writes the valid tree", [this, s, &panel]()
          {
              TreeDesc onDisk;
              std::string e;
              const bool read = ReadTreeDesc(mBehaviorSystem.AbsolutePath(s->treePath), onDisk, e);
              s->requesterBrainBefore = mCharacterSystem.Find(kRequester)->OwnBrain();
              s->talkerBrainBefore = mCharacterSystem.Find(kTalker)->OwnBrain();
              return read && !panel.Dirty() && onDisk.Find(1) != nullptr && onDisk.Find(1)->children == std::vector<uint32_t>{3, 2};
          });
    key(ImGuiKey_E, true);
    wait(3);
    check("Ctrl+E now switches to View", [this]() { return !mSceneEditor.IsEditMode(); });
    check("Edit -> View: the agent running the modified tree got a new brain from the root, the others kept theirs", [this, s]()
          {
              const auto* bt = dynamic_cast<const BehaviorTreeBrain*>(mCharacterSystem.Find(kRequester)->OwnBrain());
              return bt != nullptr && mCharacterSystem.Find(kRequester)->OwnBrain() != s->requesterBrainBefore &&
                     bt->Tree()->desc->Find(1)->children == std::vector<uint32_t>{3, 2} && mCharacterSystem.Find(kTalker)->OwnBrain() == s->talkerBrainBefore;
          });

    // live view: select the agent's instance
    s->ops.push_back({"select agent 13", [this](ImGuiIO&)
                      {
                          ModelAndInstanceData& sc = mSceneEditor.ModelData();
                          for (size_t i = 0; i < sc.miModelInstances.size(); ++i)
                              if (sc.miModelInstances[i]->GetEntityId() == kRequester)
                                  sc.miSelectedInstance = static_cast<int>(i);
                      },
                      20});
    check("View mode: the panel shows the selected agent live (ticks counted, root status shown)", [this, &panel]()
          {
              const auto* bt = dynamic_cast<const BehaviorTreeBrain*>(mCharacterSystem.Find(kRequester)->OwnBrain());
              return panel.LiveEntity() == kRequester && bt != nullptr && bt->TickCount() > 0 && bt->RootStatus() != NodeStatus::Idle;
          });

    // possession and release (D3)
    s->ops.push_back({"possess 13", [this](ImGuiIO&) { mCharacterSystem.Possess(kRequester); }, 10});
    check("possessing agent 13 parks its tree and runs the player brain", [this]()
          {
              const Agent* a = mCharacterSystem.Find(kRequester);
              return mCharacterSystem.Possessed() == kRequester && a->parkedBrain != nullptr && std::string(a->brain->Kind()) == "Player";
          });
    s->ops.push_back({"possess 12", [this](ImGuiIO&) { mCharacterSystem.Possess(kTalker); }, 10});
    check("possessing another agent releases 13: its own tree is back (started over) and ticking", [this]()
          {
              const Agent* a = mCharacterSystem.Find(kRequester);
              const auto* bt = dynamic_cast<const BehaviorTreeBrain*>(a->brain.get());
              return mCharacterSystem.Possessed() == kTalker && a->parkedBrain == nullptr && bt != nullptr && bt->TickCount() > 0;
          });
    wait(5);

    mUserInterface.inputInjector = [this](ImGuiIO& io)
    {
        EditorSmoke& sm = *mEditorSmoke;
        if (sm.finished)
        {
            return;
        }
        if (sm.mouseActive)
        {
            io.AddMousePosEvent(sm.mouse.x, sm.mouse.y);
        }
        if (sm.holdLeft > 0)
        {
            --sm.holdLeft;
            return;
        }
        if (sm.ops.empty())
        {
            sm.finished = true;
            std::error_code ec;
            std::filesystem::remove(mBehaviorSystem.AbsolutePath(sm.treePath), ec);
            fmt::print(sm.ok ? fg(fmt::color::green) : fg(fmt::color::red), "smoke[editor] {} ({} checks)\n", sm.ok ? "OK" : "FAILED", sm.checks);
            mVerifyFailed = mVerifyFailed || !sm.ok;
            mFrameLimit = mFramesRendered + 1;
            return;
        }
        EditorSmoke::Op op = std::move(sm.ops.front());
        sm.ops.pop_front();
        if (std::getenv("RANIM_EDITOR_SMOKE_TRACE") != nullptr)
        {
            ImGuiContext& g = *ImGui::GetCurrentContext();
            fmt::print("trace: {} | mouse ({:.0f}, {:.0f}) down {} | hovered '{}' | focused app {} | accepting {}\n", op.name, io.MousePos.x,
                       io.MousePos.y, io.MouseDown[0], g.HoveredWindow ? g.HoveredWindow->Name : "-", io.AppFocusLost ? 0 : 1,
                       io.AppAcceptingEvents);
        }
        op.act(io);
        if (sm.mouseActive)
        {
            io.AddMousePosEvent(sm.mouse.x, sm.mouse.y);
        }
        sm.holdLeft = op.hold;
    };
    fmt::print("smoke: editor input test, {} scripted steps\n", s->ops.size());
    return true;
}
