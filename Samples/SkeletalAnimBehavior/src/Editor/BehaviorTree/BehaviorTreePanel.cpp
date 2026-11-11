#include <Editor/BehaviorTree/BehaviorTreePanel.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>

#include <fmt/format.h>
#include <imgui.h>
#include <imgui_node_editor.h>

#include <Agent/CharacterSystem.h>
#include <Agent/PerceptionKeys.h>
#include <Behavior/BehaviorSystem.h>
#include <Behavior/BehaviorTreeBrain.h>
#include <Editor/Command.h>
#include <Editor/SceneEditor.h>
#include <Model/ModelInstance.h>

namespace ed = ax::NodeEditor;

namespace RAnimation
{
    namespace
    {
        constexpr uintptr_t kPinBase = 1u << 20;
        constexpr uintptr_t kLinkBase = 1u << 24;

        ed::NodeId NodeIdOf(uint32_t id) { return ed::NodeId(static_cast<uintptr_t>(id) + 1); }
        uint32_t NodeFromEd(ed::NodeId id) { return static_cast<uint32_t>(id.Get() - 1); }
        ed::PinId InPin(uint32_t id) { return ed::PinId(kPinBase + static_cast<uintptr_t>(id) * 2); }
        ed::PinId OutPin(uint32_t id) { return ed::PinId(kPinBase + static_cast<uintptr_t>(id) * 2 + 1); }
        ed::LinkId LinkOf(uint32_t child) { return ed::LinkId(kLinkBase + child); }

        struct PinInfo
        {
            bool valid = false;
            bool output = false;
            uint32_t node = 0;
        };
        PinInfo Decode(ed::PinId pin)
        {
            PinInfo p;
            const uintptr_t v = pin.Get();
            if (v < kPinBase || v >= kLinkBase)
                return p;
            p.valid = true;
            p.node = static_cast<uint32_t>((v - kPinBase) / 2);
            p.output = ((v - kPinBase) % 2) == 1;
            return p;
        }

        const ImVec4 kRunning(0.35f, 0.6f, 1.0f, 1.0f);
        const ImVec4 kSuccess(0.35f, 0.85f, 0.45f, 1.0f);
        const ImVec4 kFailure(1.0f, 0.35f, 0.35f, 1.0f);
        const ImVec4 kIdle(0.55f, 0.55f, 0.58f, 1.0f);
        const ImVec4 kError(1.0f, 0.4f, 0.4f, 1.0f);
        const ImVec4 kDim(0.6f, 0.6f, 0.65f, 1.0f);

        ImVec4 StatusColor(NodeStatus s)
        {
            switch (s)
            {
                case NodeStatus::Running: return kRunning;
                case NodeStatus::Success: return kSuccess;
                case NodeStatus::Failure: return kFailure;
                default: return kIdle;
            }
        }

        bool InputString(const char* label, std::string& value)
        {
            char buffer[256];
            std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
            if (ImGui::InputText(label, buffer, sizeof(buffer)))
            {
                value = buffer;
                return true;
            }
            return false;
        }

        // Edits a value of the given type in place; true when changed.
        bool EditValue(const char* label, BbType type, BbValue& v)
        {
            BbValue typed;
            if (!ConvertValue(v, type, typed))
            {
                if (!(std::holds_alternative<std::string>(v) && ValueFromString(type, std::get<std::string>(v), typed)))
                    typed = DefaultValueFor(type);
            }
            bool changed = false;
            switch (type)
            {
                case BbType::Bool:
                {
                    bool b = std::get<bool>(typed);
                    changed = ImGui::Checkbox(label, &b);
                    typed = b;
                    break;
                }
                case BbType::Int:
                {
                    int i = std::get<int>(typed);
                    changed = ImGui::InputInt(label, &i);
                    typed = i;
                    break;
                }
                case BbType::Float:
                {
                    float f = std::get<float>(typed);
                    changed = ImGui::InputFloat(label, &f, 0.0f, 0.0f, "%.3f");
                    typed = f;
                    break;
                }
                case BbType::Vec3:
                {
                    glm::vec3 p = std::get<glm::vec3>(typed);
                    changed = ImGui::InputFloat3(label, &p.x, "%.2f");
                    typed = p;
                    break;
                }
                case BbType::String:
                {
                    std::string s = std::get<std::string>(typed);
                    changed = InputString(label, s);
                    typed = s;
                    break;
                }
                case BbType::Entity:
                {
                    uint64_t e = std::get<EntityId>(typed);
                    changed = ImGui::InputScalar(label, ImGuiDataType_U64, &e);
                    typed = static_cast<EntityId>(e);
                    break;
                }
            }
            if (changed || TypeOf(v) != type)
            {
                v = typed;
            }
            return changed;
        }

        const char* kTypeNames[] = {"Bool", "Int", "Float", "Vec3", "String", "Entity"};
        const char* kScopeNames[] = {"Global", "TreeShared", "Instance"};
    } // namespace

    void BehaviorTreePanel::Remember(const std::string& what)
    {
        ImVec2 a = ImGui::GetItemRectMin();
        ImVec2 b = ImGui::GetItemRectMax();
        // items inside the node editor live in canvas space
        if (what.rfind("in:", 0) == 0 || what.rfind("out:", 0) == 0 || what.rfind("title:", 0) == 0)
        {
            a = ed::CanvasToScreen(a);
            b = ed::CanvasToScreen(b);
        }
        mScreen[what] = {0.5f * (a.x + b.x), 0.5f * (a.y + b.y)};
    }

    bool BehaviorTreePanel::ScreenPos(const std::string& what, float& x, float& y) const
    {
        const auto it = mScreen.find(what);
        if (it == mScreen.end())
        {
            return false;
        }
        x = it->second.first;
        y = it->second.second;
        return true;
    }

    void BehaviorTreePanel::Initialize(BehaviorSystem* behavior, CharacterSystem* characters)
    {
        mBehavior = behavior;
        mCharacters = characters;
        ed::Config config;
        config.SettingsFile = nullptr; // positions live in the .behaviortree.usda
        mContext = ed::CreateEditor(&config);
        ed::SetCurrentEditor(mContext);
        // opaque canvas: windows behind the editor must not show through
        ImVec4& bg = ed::GetStyle().Colors[ed::StyleColor_Bg];
        bg = ImVec4(0.11f, 0.11f, 0.13f, 1.0f);
        ed::SetCurrentEditor(nullptr);
        const std::vector<std::string> files = TreeFiles();
        if (!files.empty())
        {
            LoadTree(files.front());
        }
    }

    void BehaviorTreePanel::Shutdown()
    {
        if (mContext != nullptr)
        {
            ed::DestroyEditor(mContext);
            mContext = nullptr;
        }
    }

    std::vector<std::string> BehaviorTreePanel::TreeFiles() const
    {
        std::vector<std::string> files;
        if (mBehavior == nullptr)
        {
            return files;
        }
        std::error_code ec;
        const std::filesystem::path dir = std::filesystem::path(mBehavior->AbsolutePath("behavior"));
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
        {
            const std::string name = entry.path().filename().generic_string();
            if (name.size() > 18 && name.rfind(".behaviortree.usda") == name.size() - 18)
            {
                files.push_back("behavior/" + name);
            }
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    bool BehaviorTreePanel::LoadTree(const std::string& relativePath)
    {
        TreeDesc desc;
        std::string error;
        if (mBehavior == nullptr || !ReadTreeDesc(mBehavior->AbsolutePath(relativePath), desc, error))
        {
            mStatus = error;
            mStatusIsError = true;
            return false;
        }
        mDesc = std::move(desc);
        mPath = relativePath;
        mSavedHash = ComputeContentHash(mDesc);
        mDirty = false;
        mPositionsDirty = true;
        mSelectedNode = ~0u;
        mStatus.clear();
        mNavigateFrames = 3;
        Validate();
        return true;
    }

    void BehaviorTreePanel::Validate()
    {
        if (mBehavior == nullptr)
        {
            return;
        }
        CompileTree(std::make_shared<TreeDesc>(mDesc), mPath, mBehavior->Types(), mBehavior->GlobalSchema(), mErrors);
    }

    bool BehaviorTreePanel::Save(std::string& outError)
    {
        Validate();
        if (!mErrors.empty())
        {
            outError = fmt::format("not saved: {} validation error(s) (rule list below)", mErrors.size());
            return false;
        }
        if (!WriteTreeDesc(mDesc, mBehavior->Types(), mBehavior->AbsolutePath(mPath), outError))
        {
            return false;
        }
        if (mGlobalModified && !mBehavior->SaveGlobalSchema(outError))
        {
            return false;
        }
        mSavedHash = ComputeContentHash(mDesc);
        mDirty = false;
        mModifiedThisEdit.insert(mPath);
        return true;
    }

    bool BehaviorTreePanel::CanLeaveEdit(std::string& outReason) const
    {
        if (mDirty)
        {
            outReason = fmt::format("save '{}' first (Edit -> View needs saved, valid trees)", mPath);
            return false;
        }
        if (!mErrors.empty())
        {
            outReason = fmt::format("'{}' has validation errors", mPath);
            return false;
        }
        return true;
    }

    // D17 + 13.1 #4: replace the modified trees, rebuild their TreeShared stores, restart their agents.
    void BehaviorTreePanel::OnEnteredView()
    {
        if (mBehavior == nullptr || mCharacters == nullptr || (mModifiedThisEdit.empty() && !mGlobalModified))
        {
            return;
        }
        std::set<std::string> paths = mModifiedThisEdit;
        if (mGlobalModified)
        {
            for (const std::string& p : mBehavior->CachedPaths())
                paths.insert(p);
        }
        for (const std::string& path : paths)
        {
            auto desc = std::make_shared<TreeDesc>();
            std::string error;
            std::vector<ValidationError> errors;
            if (!ReadTreeDesc(mBehavior->AbsolutePath(path), *desc, error))
            {
                continue;
            }
            if (auto tree = CompileTree(desc, path, mBehavior->Types(), mBehavior->GlobalSchema(), errors))
            {
                mBehavior->ReplaceTree(path, tree);
            }
        }
        int restarted = 0;
        for (const auto& agent : mCharacters->Agents())
        {
            if (agent->config && paths.count(agent->config->tree) != 0)
            {
                mCharacters->RebuildBrain(*agent);
                ++restarted;
            }
        }
        mStatus = fmt::format("applied {} tree(s); {} agent(s) start over from the root", paths.size(), restarted);
        mStatusIsError = false;
        mModifiedThisEdit.clear();
        mGlobalModified = false;
    }

    // ---- edit plumbing ---------------------------------------------------------------------------------

    void BehaviorTreePanel::Begin()
    {
        mBefore = Snapshot{mDesc, mBehavior->GlobalSchema()};
        mBefore.desc.name = mDesc.name;
    }

    void BehaviorTreePanel::Apply(const Snapshot& s)
    {
        mDesc = s.desc;
        if (mBehavior != nullptr && s.global.keys.size() != mBehavior->GlobalSchema().keys.size())
        {
            mBehavior->SetGlobalSchema(s.global);
        }
        else if (mBehavior != nullptr)
        {
            bool same = true;
            for (size_t i = 0; i < s.global.keys.size(); ++i)
            {
                const BlackboardKeyDecl& a = s.global.keys[i];
                const BlackboardKeyDecl& b = mBehavior->GlobalSchema().keys[i];
                same = same && a.name == b.name && a.type == b.type && a.wakeOnChange == b.wakeOnChange && ValuesEqual(a.defaultValue, b.defaultValue);
            }
            if (!same)
            {
                mBehavior->SetGlobalSchema(s.global);
            }
        }
        mDirty = true;
        mPositionsDirty = true;
        Validate();
    }

    void BehaviorTreePanel::Commit(SceneEditor& sceneEditor, const char* name)
    {
        Snapshot after{mDesc, mBehavior->GlobalSchema()};
        Snapshot before = mBefore;
        const std::string path = mPath;
        sceneEditor.ExecuteCommand(std::make_unique<FunctionalCommand>(
                std::string("Behavior: ") + name,
                [this, after, path]()
                {
                    if (mPath == path)
                        Apply(after);
                },
                [this, before, path]()
                {
                    if (mPath == path)
                        Apply(before);
                }));
        Begin();
    }

    uint32_t BehaviorTreePanel::AddNode(const std::string& type, float x, float y)
    {
        BtNodeDesc n;
        n.id = mDesc.nextId++;
        n.type = type;
        n.editorPos = glm::vec2(x, y);
        mDesc.nodes.push_back(n);
        return n.id;
    }

    BtNodeDesc* BehaviorTreePanel::ParentOf(uint32_t id)
    {
        for (BtNodeDesc& n : mDesc.nodes)
        {
            if (std::find(n.children.begin(), n.children.end(), id) != n.children.end())
            {
                return &n;
            }
        }
        return nullptr;
    }

    bool BehaviorTreePanel::IsAncestor(uint32_t ancestor, uint32_t node) const
    {
        const BtNodeDesc* a = mDesc.Find(ancestor);
        if (a == nullptr)
            return false;
        for (uint32_t c : a->children)
        {
            if (c == node || IsAncestor(c, node))
                return true;
        }
        return false;
    }

    // ---- drawing ---------------------------------------------------------------------------------------

    void BehaviorTreePanel::Draw(SceneEditor& sceneEditor)
    {
        if (!bVisible || mContext == nullptr || mBehavior == nullptr)
        {
            mWasVisible = false;
            return;
        }
        if (!mWasVisible)
        {
            mFocusWindow = 3; // just opened
        }
        if (mFocusWindow > 0)
        {
            ImGui::SetNextWindowFocus();
            --mFocusWindow;
        }
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 work = viewport->WorkSize;
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 8.0f, viewport->WorkPos.y + 8.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::min(1250.0f, work.x - 16.0f), std::min(680.0f, work.y - 16.0f)), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(480.0f, 320.0f), ImVec2(std::max(480.0f, work.x), std::max(320.0f, work.y)));
        mScreen.clear();
        const bool open = ImGui::Begin("Behavior Tree Editor", &bVisible);
        mWasVisible = bVisible;
        if (!open)
        {
            ImGui::End();
            return;
        }
        {
            // keep the whole window inside the application window (a position saved in imgui.ini may not fit)
            const ImVec2 p = ImGui::GetWindowPos();
            const ImVec2 s = ImGui::GetWindowSize();
            const ImVec2 clamped(std::clamp(p.x, viewport->WorkPos.x, std::max(viewport->WorkPos.x, viewport->WorkPos.x + work.x - s.x)),
                                 std::clamp(p.y, viewport->WorkPos.y, std::max(viewport->WorkPos.y, viewport->WorkPos.y + work.y - s.y)));
            if (clamped.x != p.x || clamped.y != p.y)
            {
                ImGui::SetWindowPos(clamped);
            }
        }
        const bool editMode = sceneEditor.IsEditMode();
        if (!ImGui::IsAnyItemActive())
        {
            Begin(); // snapshot for the widget that becomes active next
        }

        // live agent: the selected instance's agent (View mode)
        const Agent* agent = nullptr;
        const BehaviorTreeBrain* live = nullptr;
        ModelAndInstanceData& scene = sceneEditor.ModelData();
        if (mCharacters != nullptr && !scene.miModelInstances.empty())
        {
            const auto& instance = scene.miModelInstances[std::clamp(scene.miSelectedInstance, 0, static_cast<int>(scene.miModelInstances.size()) - 1)];
            agent = mCharacters->FindByInstance(instance.get());
            const auto* bt = agent != nullptr ? dynamic_cast<const BehaviorTreeBrain*>(agent->OwnBrain()) : nullptr;
            if (bt != nullptr && bFollowSelectedAgent && !editMode && !mDirty && bt->Tree()->path != mPath && !bt->Tree()->path.empty())
            {
                LoadTree(bt->Tree()->path);
            }
            if (bt != nullptr && !editMode && bt->Tree()->path == mPath && bt->Tree()->contentHash == mSavedHash)
            {
                live = bt;
            }
        }
        mLiveEntity = live != nullptr ? agent->entity : kInvalidEntityId;

        DrawToolbar(sceneEditor, editMode);
        const float sideWidth = 380.0f;
        ImGui::BeginChild("btCanvasRegion", ImVec2(-sideWidth, 0.0f));
        DrawCanvas(sceneEditor, editMode, live);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("btSide", ImVec2(0.0f, 0.0f));
        // fields take ~55 % of the narrow side panel so their labels stay readable
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
        if (ImGui::BeginTabBar("btSideTabs"))
        {
            if (ImGui::BeginTabItem("Inspector"))
            {
                DrawInspector(sceneEditor, editMode, live, agent);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Blackboard"))
            {
                DrawBlackboard(sceneEditor, editMode, live, agent);
                ImGui::EndTabItem();
            }
            const bool errorsOpen = ImGui::BeginTabItem(fmt::format("Errors ({})###btErrors", mErrors.size()).c_str());
            Remember("tab:Errors");
            if (errorsOpen)
            {
                DrawErrors();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Tick log"))
            {
                DrawTickLog(live);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::End();
    }

    void BehaviorTreePanel::DrawToolbar(SceneEditor& sceneEditor, bool editMode)
    {
        const std::vector<std::string> files = TreeFiles();
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::BeginCombo("##btFile", mPath.empty() ? "(no tree)" : mPath.c_str()))
        {
            for (const std::string& f : files)
            {
                if (ImGui::Selectable(f.c_str(), f == mPath))
                {
                    if (mDirty)
                    {
                        mStatus = "save or revert the current tree first";
                        mStatusIsError = true;
                    }
                    else
                    {
                        LoadTree(f);
                    }
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!editMode);
        const bool savePressed = ImGui::Button("Save");
        Remember("Save");
        if (savePressed)
        {
            std::string error;
            if (Save(error))
            {
                mStatus = "saved " + mPath;
                mStatusIsError = false;
            }
            else
            {
                mStatus = error;
                mStatusIsError = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert") && !mPath.empty())
        {
            LoadTree(mPath);
        }
        ImGui::SameLine();
        if (ImGui::Button("Auto layout"))
        {
            Begin();
            AutoLayoutTree(mDesc);
            Commit(sceneEditor, "Auto layout");
            mPositionsDirty = true;
        }
        ImGui::SameLine();
        const auto& types = mBehavior->Types().All();
        ImGui::SetNextItemWidth(170.0f);
        mAddType = std::clamp(mAddType, 0, static_cast<int>(types.size()) - 1);
        if (ImGui::BeginCombo("##btAddType", types[static_cast<size_t>(mAddType)]->name.c_str()))
        {
            for (size_t i = 0; i < types.size(); ++i)
            {
                const std::string label = fmt::format("{} ({})", types[i]->name, NodeCategoryName(types[i]->category));
                if (ImGui::Selectable(label.c_str(), static_cast<int>(i) == mAddType))
                    mAddType = static_cast<int>(i);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", types[i]->doc.c_str());
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        const bool addPressed = ImGui::Button("Add node");
        Remember("Add node");
        if (addPressed)
        {
            float maxY = 0.0f, x = 0.0f;
            for (const BtNodeDesc& n : mDesc.nodes)
            {
                if (n.editorPos.y >= maxY)
                {
                    maxY = n.editorPos.y;
                    x = n.editorPos.x;
                }
            }
            Begin();
            mSelectedNode = AddNode(types[static_cast<size_t>(mAddType)]->name, x + 240.0f, maxY);
            Commit(sceneEditor, "Add node");
            mFocusNode = mSelectedNode;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputText("##btNewName", mNewTreeName, sizeof(mNewTreeName));
        ImGui::SameLine();
        if (ImGui::Button("New tree"))
        {
            TreeDesc d;
            d.name = mNewTreeName;
            d.doc = "new tree";
            d.tickHz = 10.0f;
            BtNodeDesc root;
            root.id = d.nextId++;
            root.type = "Wait";
            d.nodes.push_back(root);
            d.rootId = root.id;
            const std::string path = fmt::format("behavior/{}.behaviortree.usda", mNewTreeName);
            std::string error;
            if (std::filesystem::exists(mBehavior->AbsolutePath(path)))
            {
                mStatus = path + " already exists";
                mStatusIsError = true;
            }
            else if (WriteTreeDesc(d, mBehavior->Types(), mBehavior->AbsolutePath(path), error))
            {
                LoadTree(path);
                mStatus = "created " + path;
                mStatusIsError = false;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Checkbox("Follow selected agent", &bFollowSelectedAgent);
        ImGui::TextColored(mDirty ? kError : kDim, "%s%s  |  %s  |  %zu node(s), tick %.1f Hz%s", mPath.c_str(), mDirty ? " *" : "",
                           editMode ? "EDIT: editable, agents paused" : "VIEW: read only, live", mDesc.nodes.size(), mDesc.tickHz,
                           mLiveEntity != kInvalidEntityId ? fmt::format("  |  live: agent {}", mLiveEntity).c_str() : "");
        if (!mStatus.empty())
        {
            ImGui::TextColored(mStatusIsError ? kError : ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "%s", mStatus.c_str());
        }
    }

    void BehaviorTreePanel::DrawCanvas(SceneEditor& sceneEditor, bool editMode, const BehaviorTreeBrain* live)
    {
        ed::SetCurrentEditor(mContext);
        ed::Begin("BehaviorTreeCanvas", ImVec2(0.0f, 0.0f));
        if (mPositionsDirty)
        {
            for (const BtNodeDesc& n : mDesc.nodes)
            {
                ed::SetNodePosition(NodeIdOf(n.id), ImVec2(n.editorPos.x, n.editorPos.y));
            }
            mPositionsDirty = false;
        }
        const float now = mCharacters != nullptr ? mCharacters->WorldTime() : 0.0f;
        std::set<uint32_t> errorNodes;
        for (const ValidationError& e : mErrors)
        {
            errorNodes.insert(e.nodeId);
        }

        for (const BtNodeDesc& n : mDesc.nodes)
        {
            const NodeType* type = mBehavior->Types().Find(n.type);
            NodeStatus status = NodeStatus::Idle;
            float age = 1e9f;
            int index = -1;
            if (live != nullptr)
            {
                index = live->Tree()->IndexOf(n.id);
                if (index >= 0)
                {
                    status = live->Instance().status[static_cast<size_t>(index)];
                    const float t = live->Instance().lastTickTime[static_cast<size_t>(index)];
                    age = t >= 0.0f ? now - t : 1e9f;
                }
            }
            ImVec4 border = errorNodes.count(n.id) ? kError : (live != nullptr ? StatusColor(status) : kDim);
            if (live != nullptr)
            {
                // fade with the time since the node was ticked (2 s to the dim floor)
                const float f = std::clamp(1.0f - age / 2.0f, 0.3f, 1.0f);
                border = ImVec4(border.x * f + kIdle.x * (1 - f), border.y * f + kIdle.y * (1 - f), border.z * f + kIdle.z * (1 - f), 1.0f);
            }
            ed::PushStyleColor(ed::StyleColor_NodeBorder, border);
            ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, (live != nullptr && status == NodeStatus::Running) ? 3.0f : 1.5f);
            ed::BeginNode(NodeIdOf(n.id));

            ed::PushStyleVar(ed::StyleVar_PivotAlignment, ImVec2(0.5f, 0.0f));
            ed::PushStyleVar(ed::StyleVar_TargetDirection, ImVec2(0.0f, -1.0f));
            ed::BeginPin(InPin(n.id), ed::PinKind::Input);
            ImGui::TextDisabled("%s", n.id == mDesc.rootId ? "[root]" : "  o  ");
            Remember("in:" + std::to_string(n.id));
            ed::EndPin();
            ed::PopStyleVar(2);

            // order badge among siblings (D17)
            std::string badge;
            if (BtNodeDesc* parent = ParentOf(n.id))
            {
                const auto pos = std::find(parent->children.begin(), parent->children.end(), n.id) - parent->children.begin();
                badge = fmt::format("#{} ", pos);
            }
            ImGui::TextColored(type ? (type->category == NodeCategory::Composite ? ImVec4(0.9f, 0.8f, 0.4f, 1)
                                       : type->category == NodeCategory::Decorator ? ImVec4(0.75f, 0.6f, 1.0f, 1)
                                                                                   : ImVec4(0.6f, 0.9f, 1.0f, 1))
                                     : kError,
                               "%s%s", badge.c_str(), n.label.empty() ? n.type.c_str() : n.label.c_str());
            Remember("title:" + std::to_string(n.id));
            if (!n.label.empty())
            {
                ImGui::TextDisabled("%s", n.type.c_str());
            }
            if (type != nullptr)
            {
                for (const ParamSpec& spec : type->params)
                {
                    const ParamDesc* p = n.FindParam(spec.name);
                    std::string v = p == nullptr ? ValueToString(spec.defaultValue) : (p->bound ? "@" + p->key : ValueToString(p->literal));
                    if (v.size() > 28)
                        v = v.substr(0, 26) + "..";
                    ImGui::TextDisabled("%s = %s", spec.name.c_str(), v.c_str());
                }
            }
            if (live != nullptr && index >= 0)
            {
                const NodeMemory& m = live->Instance().memory[static_cast<size_t>(index)];
                std::string extra;
                if (type != nullptr)
                {
                    for (const std::string& b : type->builtins)
                    {
                        if (b == "currentIndex")
                            extra += fmt::format(" index {}", m.index);
                        else if (b == "iteration" || b == "attempt")
                            extra += fmt::format(" {} {}", b, m.counter);
                        else if (b == "remaining" && std::isfinite(m.wakeAt))
                            extra += fmt::format(" {:.1f} s left", std::max(0.0f, m.wakeAt - now));
                        else if (b == "requestId" && m.request != 0)
                            extra += fmt::format(" request #{}", m.request);
                        else if (b == "successCount")
                            extra += fmt::format(" ok {} / failed {}", std::popcount(m.successMask), std::popcount(m.doneMask & ~m.successMask));
                    }
                }
                ImGui::TextColored(StatusColor(status), "%s%s", NodeStatusName(status), extra.c_str());
            }
            if (type != nullptr && type->category != NodeCategory::Leaf)
            {
                ed::PushStyleVar(ed::StyleVar_PivotAlignment, ImVec2(0.5f, 1.0f));
                ed::PushStyleVar(ed::StyleVar_SourceDirection, ImVec2(0.0f, 1.0f));
                ed::BeginPin(OutPin(n.id), ed::PinKind::Output);
                ImGui::TextDisabled("  v  ");
                Remember("out:" + std::to_string(n.id));
                ed::EndPin();
                ed::PopStyleVar(2);
            }
            ed::EndNode();
            ed::PopStyleVar();
            ed::PopStyleColor();
        }

        // links: parent output -> child input; the running path is highlighted
        for (const BtNodeDesc& n : mDesc.nodes)
        {
            for (uint32_t c : n.children)
            {
                bool running = false;
                if (live != nullptr)
                {
                    const int ci = live->Tree()->IndexOf(c);
                    running = ci >= 0 && live->Instance().status[static_cast<size_t>(ci)] == NodeStatus::Running;
                }
                ed::Link(LinkOf(c), OutPin(n.id), InPin(c), running ? kRunning : ImVec4(0.7f, 0.7f, 0.75f, 1.0f), running ? 4.0f : 1.5f);
            }
        }

        HandleCreation(sceneEditor, editMode);
        HandleDeletion(sceneEditor, editMode);

        // read positions back
        bool moved = false;
        for (BtNodeDesc& n : mDesc.nodes)
        {
            const ImVec2 p = ed::GetNodePosition(NodeIdOf(n.id));
            if (std::abs(p.x - n.editorPos.x) > 0.5f || std::abs(p.y - n.editorPos.y) > 0.5f)
            {
                n.editorPos = glm::vec2(p.x, p.y);
                moved = true;
            }
        }
        if (moved && editMode)
        {
            mDirty = true;
        }
        SortChildrenByX(sceneEditor, editMode);

        // selection
        ed::NodeId selected[1];
        if (ed::GetSelectedNodes(selected, 1) == 1)
        {
            mSelectedNode = NodeFromEd(selected[0]);
        }
        if (mNavigateFrames > 0 && --mNavigateFrames == 0)
        {
            ed::NavigateToContent(0.0f);
        }
        if (mFocusNode != ~0u)
        {
            ed::SelectNode(NodeIdOf(mFocusNode));
            ed::NavigateToSelection();
            mSelectedNode = mFocusNode;
            mFocusNode = ~0u;
        }
        ed::End();
        ed::SetCurrentEditor(nullptr);
    }

    void BehaviorTreePanel::HandleCreation(SceneEditor& sceneEditor, bool editMode)
    {
        if (ed::BeginCreate(ImVec4(1, 1, 1, 1), 2.0f))
        {
            ed::PinId a, b;
            if (ed::QueryNewLink(&a, &b))
            {
                PinInfo pa = Decode(a);
                PinInfo pb = Decode(b);
                if (pa.valid && pb.valid && !pa.output && pb.output)
                {
                    std::swap(pa, pb);
                }
                std::string reason;
                BtNodeDesc* parent = pa.valid ? mDesc.Find(pa.node) : nullptr;
                const NodeType* parentType = parent ? mBehavior->Types().Find(parent->type) : nullptr;
                if (!editMode)
                    reason = "edit mode only";
                else if (!pa.valid || !pb.valid || !pa.output || pb.output)
                    reason = "connect a parent's bottom pin to a child's top pin";
                else if (parentType == nullptr || parentType->category == NodeCategory::Leaf)
                    reason = "a leaf has no children";
                else if (parentType->category == NodeCategory::Decorator && !parent->children.empty())
                    reason = "a decorator has exactly one child";
                else if (pb.node == mDesc.rootId)
                    reason = "the root has no parent";
                else if (ParentOf(pb.node) != nullptr)
                    reason = "the child already has a parent";
                else if (pb.node == pa.node || IsAncestor(pb.node, pa.node))
                    reason = "that would make a cycle";
                if (!reason.empty())
                {
                    if (ImGui::IsMouseReleased(0))
                    {
                        mStatus = reason;
                        mStatusIsError = true;
                    }
                    ed::RejectNewItem(kError, 2.0f);
                }
                else if (ed::AcceptNewItem(kSuccess, 2.0f))
                {
                    Begin();
                    mDesc.Find(pa.node)->children.push_back(pb.node);
                    BtNodeDesc* p = mDesc.Find(pa.node);
                    std::stable_sort(p->children.begin(), p->children.end(), [this](uint32_t x, uint32_t y)
                                     { return mDesc.Find(x)->editorPos.x < mDesc.Find(y)->editorPos.x; });
                    Commit(sceneEditor, "Link");
                    mStatus.clear();
                }
            }
        }
        ed::EndCreate();
    }

    void BehaviorTreePanel::HandleDeletion(SceneEditor& sceneEditor, bool editMode)
    {
        bool changed = false;
        if (ed::BeginDelete())
        {
            ed::LinkId link;
            while (ed::QueryDeletedLink(&link))
            {
                if (!editMode)
                {
                    ed::RejectDeletedItem();
                    continue;
                }
                if (ed::AcceptDeletedItem())
                {
                    if (!changed)
                        Begin();
                    changed = true;
                    const uint32_t child = static_cast<uint32_t>(link.Get() - kLinkBase);
                    if (BtNodeDesc* parent = ParentOf(child))
                    {
                        parent->children.erase(std::remove(parent->children.begin(), parent->children.end(), child), parent->children.end());
                    }
                }
            }
            ed::NodeId node;
            while (ed::QueryDeletedNode(&node))
            {
                if (!editMode)
                {
                    ed::RejectDeletedItem();
                    continue;
                }
                if (ed::AcceptDeletedItem())
                {
                    if (!changed)
                        Begin();
                    changed = true;
                    const uint32_t id = NodeFromEd(node);
                    if (BtNodeDesc* parent = ParentOf(id))
                    {
                        parent->children.erase(std::remove(parent->children.begin(), parent->children.end(), id), parent->children.end());
                    }
                    mDesc.nodes.erase(std::remove_if(mDesc.nodes.begin(), mDesc.nodes.end(), [id](const BtNodeDesc& n) { return n.id == id; }),
                                      mDesc.nodes.end());
                    if (mSelectedNode == id)
                        mSelectedNode = ~0u;
                }
            }
        }
        ed::EndDelete();
        if (changed)
        {
            Commit(sceneEditor, "Delete");
        }
    }

    // D17: children follow their x order; applied when the mouse is up (end of a drag) as one command.
    void BehaviorTreePanel::SortChildrenByX(SceneEditor& sceneEditor, bool editMode)
    {
        if (!editMode || ImGui::IsMouseDown(0))
        {
            return;
        }
        bool changed = false;
        for (BtNodeDesc& n : mDesc.nodes)
        {
            if (n.children.size() < 2)
                continue;
            std::vector<uint32_t> sorted = n.children;
            std::stable_sort(sorted.begin(), sorted.end(),
                             [this](uint32_t a, uint32_t b)
                             {
                                 const BtNodeDesc* na = mDesc.Find(a);
                                 const BtNodeDesc* nb = mDesc.Find(b);
                                 const float xa = na ? na->editorPos.x : 0.0f;
                                 const float xb = nb ? nb->editorPos.x : 0.0f;
                                 return xa < xb || (xa == xb && a < b);
                             });
            if (sorted != n.children)
            {
                if (!changed)
                {
                    // the snapshot from before the drag (positions excluded from the content, order included)
                    changed = true;
                }
                n.children = sorted;
            }
        }
        if (changed)
        {
            Commit(sceneEditor, "Reorder children (by x)");
        }
    }

    void BehaviorTreePanel::DrawInspector(SceneEditor& sceneEditor, bool editMode, const BehaviorTreeBrain* live, const Agent*)
    {
        BtNodeDesc* n = mDesc.Find(mSelectedNode);
        ImGui::BeginDisabled(!editMode);
        if (n == nullptr)
        {
            ImGui::TextDisabled("tree");
            if (InputString("name", mDesc.name) || ImGui::IsItemDeactivatedAfterEdit())
                mDirty = true;
            if (ImGui::IsItemDeactivatedAfterEdit())
                Commit(sceneEditor, "Tree name");
            InputString("doc", mDesc.doc);
            if (ImGui::IsItemDeactivatedAfterEdit())
                Commit(sceneEditor, "Tree doc");
            ImGui::InputFloat("tick Hz", &mDesc.tickHz, 0.0f, 0.0f, "%.1f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("ticks per second; 0 = every frame");
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                mDesc.tickHz = std::max(0.0f, mDesc.tickHz);
                Commit(sceneEditor, "Tick rate");
            }
            ImGui::EndDisabled();
            ImGui::TextDisabled("select a node to edit it");
            return;
        }
        const NodeType* type = mBehavior->Types().Find(n->type);
        ImGui::Text("node %u: %s (%s)%s", n->id, n->type.c_str(), type ? NodeCategoryName(type->category) : "?", n->id == mDesc.rootId ? "  [root]" : "");
        if (type != nullptr)
        {
            ImGui::TextWrapped("%s", type->doc.c_str());
        }
        InputString("label", n->label);
        if (ImGui::IsItemDeactivatedAfterEdit())
            Commit(sceneEditor, "Label");
        if (n->id != mDesc.rootId && ParentOf(n->id) == nullptr && ImGui::Button("Make root"))
        {
            Begin();
            mDesc.rootId = n->id;
            Commit(sceneEditor, "Make root");
        }
        if (type == nullptr)
        {
            ImGui::EndDisabled();
            return;
        }

        // keys this node may bind: tree keys, perception keys, global keys, node keys visible to it (D12)
        struct KeyChoice
        {
            std::string name;
            BbType type;
            const char* scope;
        };
        std::vector<KeyChoice> keys;
        for (const BlackboardKeyDecl& k : mDesc.keys)
            keys.push_back({k.name, k.type, BbScopeName(k.scope)});
        for (const BlackboardKeyDecl& k : PerceptionSchema().keys)
            keys.push_back({k.name, k.type, "perception"});
        for (const BlackboardKeyDecl& k : mBehavior->GlobalSchema().keys)
            keys.push_back({k.name, k.type, "Global"});
        {
            // own keys + the keys of the nodes this node decorates (down the decorator chain)
            const BtNodeDesc* cur = n;
            while (cur != nullptr)
            {
                for (const BlackboardKeyDecl& k : cur->nodeKeys)
                    keys.push_back({k.name, k.type, "Node"});
                const NodeType* ct = mBehavior->Types().Find(cur->type);
                cur = (ct != nullptr && ct->category == NodeCategory::Decorator && !cur->children.empty()) ? mDesc.Find(cur->children.front()) : nullptr;
            }
        }
        auto keyType = [&keys](const std::string& name, BbType& out)
        {
            for (const KeyChoice& k : keys)
                if (k.name == name)
                {
                    out = k.type;
                    return true;
                }
            return false;
        };

        ImGui::SeparatorText("parameters");
        for (size_t pi = 0; pi < type->params.size(); ++pi)
        {
            const ParamSpec& spec = type->params[pi];
            if (n->FindParam(spec.name) == nullptr)
            {
                // an unauthored parameter is shown (and saved, once edited) with the type's default
                ParamDesc d;
                d.name = spec.name;
                d.literal = spec.defaultValue;
                d.bound = spec.kind == ParamKind::Key;
                n->params.push_back(d);
            }
            ParamDesc& p = n->Param(spec.name);
            ImGui::PushID(static_cast<int>(pi));
            bool canBind = spec.kind != ParamKind::Enum;
            bool canLiteral = spec.kind != ParamKind::Key;
            if (canBind && canLiteral)
            {
                bool bound = p.bound;
                if (ImGui::Checkbox("@", &bound))
                {
                    p.bound = bound;
                    Commit(sceneEditor, "Bind parameter");
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("bind to a blackboard key");
                ImGui::SameLine();
            }
            else if (spec.kind == ParamKind::Key)
            {
                p.bound = true;
            }
            if (p.bound)
            {
                if (ImGui::BeginCombo(spec.name.c_str(), p.key.empty() ? "(choose a key)" : p.key.c_str()))
                {
                    for (const KeyChoice& k : keys)
                    {
                        bool compatible = true;
                        if (spec.kind == ParamKind::Key && !spec.keyTypes.empty())
                            compatible = std::find(spec.keyTypes.begin(), spec.keyTypes.end(), k.type) != spec.keyTypes.end();
                        else if (spec.kind == ParamKind::Value && spec.sameTypeAs < 0)
                            compatible = k.type == spec.type ||
                                         ((k.type == BbType::Int || k.type == BbType::Float || k.type == BbType::Bool) &&
                                          (spec.type == BbType::Int || spec.type == BbType::Float || spec.type == BbType::Bool));
                        if (!compatible)
                            continue;
                        if (ImGui::Selectable(fmt::format("{}  ({} {})", k.name, k.scope, BbTypeName(k.type)).c_str(), k.name == p.key))
                        {
                            p.key = k.name;
                            Commit(sceneEditor, "Bind parameter");
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            else if (spec.kind == ParamKind::Enum)
            {
                std::string current = std::holds_alternative<std::string>(p.literal) ? std::get<std::string>(p.literal) : std::string();
                if (std::find(spec.options.begin(), spec.options.end(), current) == spec.options.end())
                    current = std::get<std::string>(spec.defaultValue);
                if (ImGui::BeginCombo(spec.name.c_str(), current.c_str()))
                {
                    for (const std::string& o : spec.options)
                    {
                        if (ImGui::Selectable(o.c_str(), o == current))
                        {
                            p.literal = o;
                            Commit(sceneEditor, "Parameter");
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            else
            {
                BbType t = spec.type;
                if (spec.sameTypeAs >= 0)
                {
                    const ParamDesc* other = n->FindParam(type->params[static_cast<size_t>(spec.sameTypeAs)].name);
                    if (other == nullptr || !keyType(other->key, t))
                        t = BbType::String;
                }
                EditValue(spec.name.c_str(), t, p.literal);
                if (ImGui::IsItemDeactivatedAfterEdit())
                    Commit(sceneEditor, "Parameter");
            }
            if (!spec.doc.empty() && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", spec.doc.c_str());
            ImGui::PopID();
        }

        ImGui::SeparatorText("node keys (D12)");
        for (size_t k = 0; k < n->nodeKeys.size(); ++k)
        {
            BlackboardKeyDecl& key = n->nodeKeys[k];
            ImGui::PushID(1000 + static_cast<int>(k));
            ImGui::Text("%s (%s)", key.name.c_str(), BbTypeName(key.type));
            ImGui::SameLine();
            if (ImGui::SmallButton("x"))
            {
                Begin();
                n->nodeKeys.erase(n->nodeKeys.begin() + static_cast<long>(k));
                Commit(sceneEditor, "Remove node key");
                ImGui::PopID();
                break;
            }
            EditValue("default", key.type, key.defaultValue);
            if (ImGui::IsItemDeactivatedAfterEdit())
                Commit(sceneEditor, "Node key default");
            if (live != nullptr)
            {
                const int index = live->Tree()->IndexOf(n->id);
                if (index >= 0 && k < live->Instance().nodeKeys[static_cast<size_t>(index)].Size())
                    ImGui::TextColored(kRunning, "live: %s", ValueToString(live->Instance().nodeKeys[static_cast<size_t>(index)].Get(static_cast<uint32_t>(k))).c_str());
            }
            ImGui::PopID();
        }
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputText("##nk", mNewKeyName, sizeof(mNewKeyName));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::Combo("##nkt", &mNewKeyType, kTypeNames, 6);
        ImGui::SameLine();
        if (ImGui::Button("Add node key"))
        {
            Begin();
            BlackboardKeyDecl k;
            k.name = mNewKeyName;
            k.type = static_cast<BbType>(mNewKeyType);
            k.scope = BbScope::Node;
            k.defaultValue = DefaultValueFor(k.type);
            k.ownerNode = n->id;
            n->nodeKeys.push_back(k);
            Commit(sceneEditor, "Add node key");
        }
        ImGui::EndDisabled();
        if (!type->builtins.empty())
        {
            ImGui::SeparatorText("built-in (read only)");
            const int index = live != nullptr ? live->Tree()->IndexOf(n->id) : -1;
            for (const std::string& b : type->builtins)
            {
                std::string value = "-";
                if (index >= 0)
                {
                    const NodeMemory& m = live->Instance().memory[static_cast<size_t>(index)];
                    const float now = mCharacters != nullptr ? mCharacters->WorldTime() : 0.0f;
                    if (b == "currentIndex")
                        value = std::to_string(m.index);
                    else if (b == "iteration" || b == "attempt")
                        value = std::to_string(m.counter);
                    else if (b == "remaining")
                        value = std::isfinite(m.wakeAt) ? fmt::format("{:.2f} s", std::max(0.0f, m.wakeAt - now)) : "-";
                    else if (b == "requestId")
                        value = std::to_string(m.request);
                    else if (b == "successCount")
                        value = std::to_string(std::popcount(m.successMask));
                    else if (b == "failureCount")
                        value = std::to_string(std::popcount(m.doneMask & ~m.successMask));
                }
                ImGui::TextDisabled("%s = %s", b.c_str(), value.c_str());
            }
        }
    }

    void BehaviorTreePanel::DrawBlackboard(SceneEditor& sceneEditor, bool editMode, const BehaviorTreeBrain* live, const Agent* agent)
    {
        auto liveValue = [](const BlackboardStore* store, const std::string& name) -> std::string
        {
            if (store == nullptr)
                return {};
            const int slot = store->Find(name);
            if (slot < 0)
                return {};
            return store->IsSet(static_cast<uint32_t>(slot)) ? ValueToString(store->Get(static_cast<uint32_t>(slot))) : "(unset)";
        };
        const BlackboardStore* instanceStore = live != nullptr && agent != nullptr ? &agent->instanceBlackboard : nullptr;
        if (!ImGui::BeginTabBar("bbScopes"))
        {
            return;
        }
        // tree keys (Instance / TreeShared / Global references)
        for (int scopeTab = 0; scopeTab < 3; ++scopeTab)
        {
            const BbScope scope = scopeTab == 0 ? BbScope::Global : (scopeTab == 1 ? BbScope::TreeShared : BbScope::Instance);
            if (!ImGui::BeginTabItem(kScopeNames[scopeTab]))
                continue;
            if (scope == BbScope::Global)
            {
                ImGui::TextDisabled("global.blackboard.usda (shared by every tree)");
                BlackboardSchema schema = mBehavior->GlobalSchema();
                ImGui::BeginDisabled(!editMode);
                for (size_t k = 0; k < schema.keys.size(); ++k)
                {
                    BlackboardKeyDecl& key = schema.keys[k];
                    ImGui::PushID(static_cast<int>(k));
                    ImGui::Text("%s (%s)%s", key.name.c_str(), BbTypeName(key.type), key.wakeOnChange ? " wake" : "");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x"))
                    {
                        Begin();
                        schema.keys.erase(schema.keys.begin() + static_cast<long>(k));
                        mBehavior->SetGlobalSchema(schema);
                        mGlobalModified = true;
                        Commit(sceneEditor, "Remove global key");
                        mDirty = true;
                        ImGui::PopID();
                        break;
                    }
                    ImGui::EndDisabled();
                    const std::string v = liveValue(&mBehavior->Global(), key.name);
                    ImGui::TextColored(kRunning, "  value: %s", v.c_str());
                    ImGui::BeginDisabled(!editMode);
                    ImGui::PopID();
                }
                ImGui::EndDisabled();
            }
            else
            {
                ImGui::BeginDisabled(!editMode);
                for (size_t k = 0; k < mDesc.keys.size(); ++k)
                {
                    BlackboardKeyDecl& key = mDesc.keys[k];
                    if (key.scope != scope)
                        continue;
                    ImGui::PushID(static_cast<int>(k));
                    ImGui::Text("%s (%s)", key.name.c_str(), BbTypeName(key.type));
                    ImGui::SameLine();
                    if (ImGui::Checkbox("wake", &key.wakeOnChange))
                        Commit(sceneEditor, "Key wake flag");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x"))
                    {
                        Begin();
                        mDesc.keys.erase(mDesc.keys.begin() + static_cast<long>(k));
                        Commit(sceneEditor, "Remove key");
                        ImGui::PopID();
                        break;
                    }
                    EditValue("default", key.type, key.defaultValue);
                    if (ImGui::IsItemDeactivatedAfterEdit())
                        Commit(sceneEditor, "Key default");
                    ImGui::EndDisabled();
                    const std::string v = liveValue(scope == BbScope::TreeShared ? (live ? live->TreeShared() : nullptr) : instanceStore, key.name);
                    if (!v.empty())
                        ImGui::TextColored(kRunning, "  live: %s", v.c_str());
                    ImGui::BeginDisabled(!editMode);
                    ImGui::PopID();
                }
                ImGui::EndDisabled();
                if (scope == BbScope::Instance)
                {
                    ImGui::SeparatorText("perception (every agent)");
                    for (const BlackboardKeyDecl& key : PerceptionSchema().keys)
                    {
                        const std::string v = instanceStore != nullptr ? liveValue(instanceStore, key.name) : std::string("-");
                        ImGui::TextDisabled("%s (%s)%s = %s", key.name.c_str(), BbTypeName(key.type), key.wakeOnChange ? " wake" : "", v.c_str());
                    }
                }
            }
            // add a key to this scope
            ImGui::BeginDisabled(!editMode);
            ImGui::Separator();
            ImGui::SetNextItemWidth(140.0f);
            ImGui::InputText("##bbname", mNewKeyName, sizeof(mNewKeyName));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            ImGui::Combo("##bbtype", &mNewKeyType, kTypeNames, 6);
            ImGui::SameLine();
            if (ImGui::Button("Add key"))
            {
                Begin();
                BlackboardKeyDecl k;
                k.name = mNewKeyName;
                k.type = static_cast<BbType>(mNewKeyType);
                k.scope = scope;
                k.defaultValue = DefaultValueFor(k.type);
                if (scope == BbScope::Global)
                {
                    BlackboardSchema schema = mBehavior->GlobalSchema();
                    k.wakeOnChange = true;
                    schema.keys.push_back(k);
                    mBehavior->SetGlobalSchema(schema);
                    mGlobalModified = true;
                    mDirty = true;
                }
                else
                {
                    mDesc.keys.push_back(k);
                }
                Commit(sceneEditor, "Add key");
            }
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Node"))
        {
            const BtNodeDesc* n = mDesc.Find(mSelectedNode);
            if (n == nullptr)
            {
                ImGui::TextDisabled("select a node (its keys are edited in the Inspector)");
            }
            else
            {
                const int index = live != nullptr ? live->Tree()->IndexOf(n->id) : -1;
                for (size_t k = 0; k < n->nodeKeys.size(); ++k)
                {
                    std::string v = "-";
                    if (index >= 0 && k < live->Instance().nodeKeys[static_cast<size_t>(index)].Size())
                        v = ValueToString(live->Instance().nodeKeys[static_cast<size_t>(index)].Get(static_cast<uint32_t>(k)));
                    ImGui::Text("%s (%s) = %s", n->nodeKeys[k].name.c_str(), BbTypeName(n->nodeKeys[k].type), v.c_str());
                }
                if (n->nodeKeys.empty())
                    ImGui::TextDisabled("no node keys on node %u", n->id);
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    void BehaviorTreePanel::DrawErrors()
    {
        if (mErrors.empty())
        {
            ImGui::TextColored(kSuccess, "valid (rules 1-7 of 9.2)");
            return;
        }
        for (size_t i = 0; i < mErrors.size(); ++i)
        {
            const ValidationError& e = mErrors[i];
            const std::string text = e.nodeId == ~0u ? e.message : fmt::format("node {}: {}", e.nodeId, e.message);
            ImGui::PushStyleColor(ImGuiCol_Text, kError);
            const bool clicked = ImGui::Selectable(fmt::format("{}##err{}", text, i).c_str());
            Remember("error" + std::to_string(i));
            if (clicked && e.nodeId != ~0u)
            {
                mFocusNode = e.nodeId;
            }
            ImGui::PopStyleColor();
        }
    }

    void BehaviorTreePanel::DrawTickLog(const BehaviorTreeBrain* live)
    {
        if (live == nullptr)
        {
            ImGui::TextDisabled("View mode: select an agent running this tree");
            return;
        }
        const std::deque<TickLogEntry>& log = live->Log();
        ImGui::TextDisabled("%zu entries (last %zu kept), %llu ticks", log.size(), BehaviorTreeBrain::kLogCapacity,
                            static_cast<unsigned long long>(live->TickCount()));
        for (auto it = log.rbegin(); it != log.rend(); ++it)
        {
            const int index = live->Tree()->IndexOf(it->nodeId);
            const std::string path = index >= 0 ? live->Tree()->nodes[static_cast<size_t>(index)].path : std::to_string(it->nodeId);
            if (it->from != it->to)
                ImGui::TextColored(StatusColor(it->to), "%7.2f  %s  %s -> %s %s", it->time, path.c_str(), NodeStatusName(it->from), NodeStatusName(it->to),
                                   it->text.c_str());
            else
                ImGui::TextDisabled("%7.2f  %s  %s", it->time, path.c_str(), it->text.c_str());
        }
    }
} // namespace RAnimation
