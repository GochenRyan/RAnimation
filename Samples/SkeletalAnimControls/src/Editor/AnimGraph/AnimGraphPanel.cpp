#include <Editor/AnimGraph/AnimGraphPanel.h>

#include <algorithm>
#include <cstring>
#include <filesystem>

#include <fmt/format.h>
#include <imgui.h>
#include <imgui_node_editor.h>
#include <ImGuiFileDialog.h>

#include <AnimGraph/AnimGraphInstance.h>
#include <AnimGraph/AnimGraphUsdIo.h>
#include <Editor/AnimGraph/GraphModel.h>
#include <Editor/Command.h>
#include <Editor/SceneEditor.h>
#include <Model/Model.h>
#include <Model/ModelInstance.h>

namespace ed = ax::NodeEditor;

namespace RAnimation
{
    namespace
    {
        // node-editor ids: nodes use the editor node id; pins and links live in disjoint ranges.
        constexpr uintptr_t kPinBase = 1u << 20;
        constexpr uintptr_t kLinkBase = 1u << 24;
        constexpr uintptr_t kPinsPerNode = 64;
        constexpr uintptr_t kOutputPinOffset = 32;

        ed::PinId InputPinId(int nodeId, size_t pinIndex)
        {
            return ed::PinId(kPinBase + static_cast<uintptr_t>(nodeId) * kPinsPerNode + pinIndex);
        }
        ed::PinId OutputPinId(int nodeId, size_t pinIndex)
        {
            return ed::PinId(kPinBase + static_cast<uintptr_t>(nodeId) * kPinsPerNode + kOutputPinOffset + pinIndex);
        }
        ed::LinkId LinkIdOf(int linkId)
        {
            return ed::LinkId(kLinkBase + static_cast<uintptr_t>(linkId));
        }

        struct DecodedPin
        {
            bool valid = false;
            bool output = false;
            int nodeId = 0;
            size_t pinIndex = 0;
        };

        DecodedPin DecodePin(ed::PinId id)
        {
            DecodedPin d;
            const uintptr_t v = id.Get();
            if (v < kPinBase || v >= kLinkBase)
            {
                return d;
            }
            const uintptr_t rel = v - kPinBase;
            d.nodeId = static_cast<int>(rel / kPinsPerNode);
            const uintptr_t idx = rel % kPinsPerNode;
            d.output = idx >= kOutputPinOffset;
            d.pinIndex = d.output ? idx - kOutputPinOffset : idx;
            d.valid = true;
            return d;
        }

        std::string PinName(const EditorGraph& graph, const DecodedPin& pin)
        {
            const EditorNode* node = graph.FindNode(pin.nodeId);
            const NodeTypeDesc* type = node != nullptr ? FindNodeType(node->Type) : nullptr;
            if (type == nullptr)
            {
                return {};
            }
            const std::vector<PinDesc>& pins = pin.output ? type->Outputs : type->Inputs;
            return pin.pinIndex < pins.size() ? pins[pin.pinIndex].Name : std::string{};
        }

        size_t PinIndexByName(const std::vector<PinDesc>& pins, const std::string& name)
        {
            for (size_t i = 0; i < pins.size(); ++i)
            {
                if (pins[i].Name == name)
                {
                    return i;
                }
            }
            return 0;
        }

        const ImVec4 kGpuColor(0.35f, 0.85f, 0.45f, 1.0f);
        const ImVec4 kCpuColor(0.95f, 0.65f, 0.25f, 1.0f);
        const ImVec4 kSeamColor(1.0f, 0.55f, 0.15f, 1.0f);
        const ImVec4 kLinkColor(0.75f, 0.75f, 0.8f, 1.0f);
        const ImVec4 kErrorColor(1.0f, 0.35f, 0.35f, 1.0f);
    } // namespace

    void AnimGraphPanel::Initialize()
    {
        ed::Config config;
        config.SettingsFile = nullptr; // positions are persisted in the .animgraph.usda, not an ini
        mContext = ed::CreateEditor(&config);
        if (!mAsset)
        {
            ReplaceGraph(AnimGraphAsset::MakeSingleClip(0, 1.0f, true));
            mAsset->bImplicit = false;
            mAsset->Name = "untitled";
            mAsset->Doc = "new graph";
        }
    }

    void AnimGraphPanel::Shutdown()
    {
        if (mContext != nullptr)
        {
            ed::DestroyEditor(mContext);
            mContext = nullptr;
        }
    }

    void AnimGraphPanel::ReloadPresetList()
    {
        // PresetDir/<family>/*.animgraph.usda. Files directly in PresetDir belong to no family and are
        // not listed: a graph only makes sense for the skeleton family whose clip names it uses.
        mFamilies.clear();
        mPresetFiles.clear();
        std::error_code ec;
        if (!PresetDir.empty() && std::filesystem::is_directory(PresetDir, ec))
        {
            for (const auto& familyEntry : std::filesystem::directory_iterator(PresetDir, ec))
            {
                if (!familyEntry.is_directory())
                {
                    continue;
                }
                const std::string family = familyEntry.path().filename().generic_string();
                std::vector<std::string>& files = mPresetFiles[family];
                for (const auto& entry : std::filesystem::directory_iterator(familyEntry.path(), ec))
                {
                    const std::string name = entry.path().filename().generic_string();
                    if (entry.is_regular_file() && name.size() > 15 && name.compare(name.size() - 15, 15, ".animgraph.usda") == 0)
                    {
                        files.push_back(entry.path().generic_string());
                    }
                }
                std::sort(files.begin(), files.end());
                mFamilies.push_back(family);
            }
            std::sort(mFamilies.begin(), mFamilies.end());
        }
        bPresetListLoaded = true;
    }

    const std::vector<std::string>& AnimGraphPanel::Families()
    {
        if (!bPresetListLoaded)
        {
            ReloadPresetList();
        }
        return mFamilies;
    }

    const std::vector<std::string>& AnimGraphPanel::PresetFiles(const std::string& family)
    {
        if (!bPresetListLoaded)
        {
            ReloadPresetList();
        }
        static const std::vector<std::string> kNone;
        const auto it = mPresetFiles.find(family);
        return it != mPresetFiles.end() ? it->second : kNone;
    }

    void AnimGraphPanel::FollowModelFamily(const std::string& family)
    {
        if (family != mFollowedModelFamily)
        {
            // A different model got selected: browse its family again even if the user had pinned one.
            mFollowedModelFamily = family;
            bFamilyPinned = false;
        }
        if (!bFamilyPinned && !family.empty())
        {
            mFamily = family;
        }
        if (mFamily.empty() && !Families().empty())
        {
            mFamily = Families().front();
        }
    }

    void AnimGraphPanel::ReplaceGraph(std::shared_ptr<AnimGraphAsset> asset)
    {
        mAsset = std::move(asset);
        mGraph.FromGraphDesc(mAsset->EditorDesc, mAsset->Layout);
        mPresetDoc = mAsset->Doc;
        FilePath = mAsset->Path;
        NotifyGraphReplaced();
        RefreshGpuPlan();
    }

    std::shared_ptr<AnimGraphAsset> AnimGraphPanel::LoadGraphFile(const std::string& path)
    {
        std::string error;
        std::shared_ptr<AnimGraphAsset> asset = AnimGraphAsset::LoadFromFile(path, error);
        if (!asset)
        {
            mStatusMessage = error;
            mStatusIsError = true;
            return nullptr;
        }
        ReplaceGraph(asset);
        mStatusMessage = fmt::format("loaded {}", asset->Name);
        mStatusIsError = false;
        return asset;
    }

    bool AnimGraphPanel::SaveTo(const std::string& path)
    {
        if (!mAsset)
        {
            return false;
        }
        mGraph.SyncToAsset(*mAsset);
        std::string error;
        if (!SaveAnimGraphUsd(mAsset->EditorDesc, mAsset->Layout, mAsset->Doc, path, error))
        {
            mStatusMessage = error;
            mStatusIsError = true;
            return false;
        }
        FilePath = path;
        mAsset->Path = path;
        std::string stem = std::filesystem::path(path).filename().generic_string();
        const std::string suffix = ".animgraph.usda";
        if (stem.size() > suffix.size() && stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0)
        {
            stem.erase(stem.size() - suffix.size());
        }
        mAsset->Name = stem;
        mAsset->bImplicit = false;
        mStatusMessage = fmt::format("saved {}", path);
        mStatusIsError = false;
        ReloadPresetList();
        return true;
    }

    void AnimGraphPanel::SyncAsset()
    {
        if (mAsset)
        {
            mGraph.SyncToAsset(*mAsset);
        }
        RefreshGpuPlan();
    }

    void AnimGraphPanel::BeginEdit()
    {
        mEditBefore = mGraph;
    }

    void AnimGraphPanel::CommitEdit(SceneEditor& sceneEditor, const char* name)
    {
        SyncAsset();
        const EditorGraph before = mEditBefore;
        const EditorGraph after = mGraph;
        sceneEditor.RecordCommand(std::make_unique<FunctionalCommand>(
                name,
                [this, after]()
                {
                    mGraph = after;
                    SyncAsset();
                    NotifyGraphReplaced();
                },
                [this, before]()
                {
                    mGraph = before;
                    SyncAsset();
                    NotifyGraphReplaced();
                }));
    }

    void AnimGraphPanel::RefreshGpuPlan()
    {
        mSideByNodeId.clear();
        mSeamSlotOf.clear();
        std::vector<int> idByIndex;
        GraphDesc desc = mGraph.ToGraphDesc(&idByIndex);
        const std::vector<int> remap = FoldCpuOnly(desc); // editor index -> runtime index (-1 = marker)
        mExec = AnalyzeExecPlan(desc);
        mPlanSummary = mExec.Summary;

        auto runtimeIndexOf = [&](int editorId) -> int
        {
            // Follow CpuOnly markers to their real source.
            int guard = 0;
            int id = editorId;
            while (guard++ < 64)
            {
                const EditorNode* node = mGraph.FindNode(id);
                if (node == nullptr)
                {
                    return -1;
                }
                if (node->Type != "CpuOnly")
                {
                    break;
                }
                const EditorLink* link = mGraph.FindLinkTo(id, "Source");
                if (link == nullptr)
                {
                    return -1;
                }
                id = link->FromNode;
            }
            for (size_t i = 0; i < idByIndex.size(); ++i)
            {
                if (idByIndex[i] == id)
                {
                    return i < remap.size() ? remap[i] : -1;
                }
            }
            return -1;
        };

        for (size_t i = 0; i < idByIndex.size(); ++i)
        {
            const int rt = i < remap.size() ? remap[i] : -1;
            mSideByNodeId[idByIndex[i]] = (rt >= 0 && static_cast<size_t>(rt) < mExec.Sides.size()) ? mExec.Sides[static_cast<size_t>(rt)] : NodeSide::Cpu;
        }
        for (const EditorLink& link : mGraph.Links)
        {
            const int consumer = runtimeIndexOf(link.ToNode);
            const int producer = runtimeIndexOf(link.FromNode);
            if (consumer < 0 || producer < 0)
            {
                continue;
            }
            if (mExec.Sides[static_cast<size_t>(consumer)] == NodeSide::Gpu && mExec.Sides[static_cast<size_t>(producer)] == NodeSide::Cpu)
            {
                mSeamSlotOf[link.Id] = mExec.SlotOf(producer);
            }
        }
    }

    void AnimGraphPanel::HandleDialogs(SceneEditor& sceneEditor)
    {
        (void)sceneEditor;
        if (ImGuiFileDialog::Instance()->Display("LoadAnimGraph"))
        {
            if (ImGuiFileDialog::Instance()->IsOk())
            {
                LoadGraphFile(ImGuiFileDialog::Instance()->GetFilePathName());
            }
            ImGuiFileDialog::Instance()->Close();
        }
        if (ImGuiFileDialog::Instance()->Display("SaveAnimGraph"))
        {
            if (ImGuiFileDialog::Instance()->IsOk())
            {
                SaveTo(ImGuiFileDialog::Instance()->GetFilePathName());
            }
            ImGuiFileDialog::Instance()->Close();
        }
    }

    void AnimGraphPanel::DrawPresetPicker(SceneEditor& sceneEditor, bool editMode)
    {
        (void)sceneEditor;
        (void)editMode;
        // Family (skeleton) combo, then the presets of that family.
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::BeginCombo("##family", mFamily.empty() ? "(no family)" : mFamily.c_str()))
        {
            for (const std::string& family : Families())
            {
                if (ImGui::Selectable(family.c_str(), family == mFamily))
                {
                    mFamily = family;
                    bFamilyPinned = family != mFollowedModelFamily;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("skeleton family = sub directory of %s\nfollows the selected instance's model unless picked by hand",
                              PresetDir.c_str());
        }
        ImGui::SameLine();
        const std::vector<std::string>& presets = PresetFiles(mFamily);
        const std::string current = mAsset ? mAsset->Name : "(none)";
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::BeginCombo("##preset", current.c_str()))
        {
            for (const std::string& path : presets)
            {
                const std::string name = std::filesystem::path(path).filename().generic_string();
                const bool selected = mAsset && mAsset->Path == path;
                if (ImGui::Selectable(name.c_str(), selected))
                {
                    LoadGraphFile(path);
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("graph presets in %s", FamilyDir().c_str());
        }
    }

    std::string AnimGraphPanel::FamilyDir() const
    {
        return mFamily.empty() ? PresetDir : (std::filesystem::path(PresetDir) / mFamily).generic_string();
    }

    void AnimGraphPanel::DrawToolbar(SceneEditor& sceneEditor)
    {
        const bool editMode = sceneEditor.IsEditMode();
        ModelAndInstanceData& scene = sceneEditor.ModelData();
        std::shared_ptr<ModelInstance> selected;
        if (!scene.miModelInstances.empty())
        {
            selected = scene.miModelInstances[std::clamp(scene.miSelectedInstance, 0, static_cast<int>(scene.miModelInstances.size()) - 1)];
        }
        const int clipCount = selected ? static_cast<int>(selected->GetModel()->GetAnimClips().size()) : -1;

        if (ImGui::Button("Validate"))
        {
            const std::vector<std::string> errors = mGraph.Validate(clipCount);
            if (errors.empty())
            {
                mStatusMessage = "graph is valid: " + mPlanSummary;
                mStatusIsError = false;
            }
            else
            {
                mStatusMessage.clear();
                for (const std::string& e : errors)
                {
                    mStatusMessage += (mStatusMessage.empty() ? "" : " | ") + e;
                }
                mStatusIsError = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Save"))
        {
            if (FilePath.empty())
            {
                IGFD::FileDialogConfig config;
                config.path = FamilyDir();
                config.fileName = "untitled.animgraph.usda";
                config.countSelectionMax = 1;
                config.flags = ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_ConfirmOverwrite;
                ImGuiFileDialog::Instance()->OpenDialog("SaveAnimGraph", "Save AnimGraph", "AnimGraph{.animgraph.usda,.usda}", config);
            }
            else
            {
                SaveTo(FilePath);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Save As..."))
        {
            IGFD::FileDialogConfig config;
            config.path = FamilyDir();
            config.fileName = FilePath.empty() ? "untitled.animgraph.usda" : std::filesystem::path(FilePath).filename().generic_string();
            config.countSelectionMax = 1;
            config.flags = ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_ConfirmOverwrite;
            ImGuiFileDialog::Instance()->OpenDialog("SaveAnimGraph", "Save AnimGraph", "AnimGraph{.animgraph.usda,.usda}", config);
        }
        ImGui::SameLine();
        if (ImGui::Button("Load..."))
        {
            IGFD::FileDialogConfig config;
            config.path = FamilyDir();
            config.countSelectionMax = 1;
            config.flags = ImGuiFileDialogFlags_Modal;
            ImGuiFileDialog::Instance()->OpenDialog("LoadAnimGraph", "Load AnimGraph", "AnimGraph{.animgraph.usda,.usda}", config);
        }
        ImGui::SameLine();
        DrawPresetPicker(sceneEditor, editMode);
        ImGui::SameLine();
        if (ImGui::Button("Rescan"))
        {
            ReloadPresetList();
        }

        // second row: add node / overlay / apply
        ImGui::BeginDisabled(!editMode);
        const std::vector<NodeTypeDesc>& registry = NodeTypeRegistry();
        mAddNodeType = std::clamp(mAddNodeType, 0, static_cast<int>(registry.size()) - 1);
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::BeginCombo("##addnode", registry[static_cast<size_t>(mAddNodeType)].TypeName.c_str()))
        {
            for (int i = 0; i < static_cast<int>(registry.size()); ++i)
            {
                if (ImGui::Selectable(registry[static_cast<size_t>(i)].TypeName.c_str(), i == mAddNodeType))
                {
                    mAddNodeType = i;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Node"))
        {
            BeginEdit();
            const float x = 80.0f + 40.0f * static_cast<float>(mGraph.Nodes.size() % 8);
            const float y = 80.0f + 40.0f * static_cast<float>(mGraph.Nodes.size() % 8);
            mPendingPlacement = mGraph.AddNode(registry[static_cast<size_t>(mAddNodeType)].TypeName, x, y);
            CommitEdit(sceneEditor, "Add Node");
        }
        ImGui::SameLine();
        if (ImGui::Button("New Graph"))
        {
            std::shared_ptr<AnimGraphAsset> asset = AnimGraphAsset::MakeSingleClip(0, 1.0f, true);
            asset->bImplicit = false;
            asset->Name = "untitled";
            asset->Doc = "new graph";
            asset->Path.clear();
            ReplaceGraph(asset);
            FilePath.clear();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Checkbox("GPU overlay", &bShowGpuOverlay);
        ImGui::SameLine();
        ImGui::BeginDisabled(!editMode || !selected || !mAsset);
        if (ImGui::Button("Apply to selected instance"))
        {
            SyncAsset();
            std::shared_ptr<AnimGraphAsset> asset = mAsset;
            std::shared_ptr<AnimGraphAsset> previous = selected->GetGraphAsset();
            std::shared_ptr<ModelInstance> instance = selected;
            sceneEditor.ExecuteCommand(std::make_unique<FunctionalCommand>(
                    "Apply Graph",
                    [instance, asset]() { instance->SetGraphAsset(asset); },
                    [instance, previous]() { instance->SetGraphAsset(previous); }));
            mStatusMessage = fmt::format("applied '{}' to the selected instance", asset->Name);
            mStatusIsError = false;
        }
        ImGui::EndDisabled();

        if (!mPresetDoc.empty())
        {
            ImGui::TextDisabled("%s", mPresetDoc.c_str());
        }
        if (bShowGpuOverlay)
        {
            ImGui::TextColored(kGpuColor, "plan: %s", mPlanSummary.c_str());
        }
    }

    void AnimGraphPanel::DrawNodes(SceneEditor& sceneEditor, ModelInstance* selected)
    {
        const bool editMode = sceneEditor.IsEditMode();
        const std::vector<std::shared_ptr<AnimClip>>* clips = selected ? &selected->GetModel()->GetAnimClips() : nullptr;

        for (EditorNode& node : mGraph.Nodes)
        {
            const NodeTypeDesc* type = FindNodeType(node.Type);
            if (type == nullptr)
            {
                continue;
            }
            const NodeSide side = mSideByNodeId.count(node.Id) ? mSideByNodeId[node.Id] : NodeSide::Cpu;

            ed::BeginNode(ed::NodeId(static_cast<uintptr_t>(node.Id)));
            ImGui::PushID(node.Id);

            ImGui::TextUnformatted(node.Type.c_str());
            if (bShowGpuOverlay)
            {
                ImGui::SameLine();
                ImGui::TextColored(side == NodeSide::Gpu ? kGpuColor : kCpuColor, side == NodeSide::Gpu ? "[GPU]" : "[CPU]");
            }

            // inputs
            for (size_t p = 0; p < type->Inputs.size(); ++p)
            {
                ed::BeginPin(InputPinId(node.Id, p), ed::PinKind::Input);
                const EditorLink* link = mGraph.FindLinkTo(node.Id, type->Inputs[p].Name);
                const auto seam = link != nullptr ? mSeamSlotOf.find(link->Id) : mSeamSlotOf.end();
                if (seam != mSeamSlotOf.end() && bShowGpuOverlay)
                {
                    ImGui::TextColored(kSeamColor, "-> %s [slot %d]", type->Inputs[p].Name.c_str(), seam->second);
                }
                else
                {
                    ImGui::Text("-> %s", type->Inputs[p].Name.c_str());
                }
                ed::EndPin();
            }

            ImGui::PushItemWidth(140.0f);
            ImGui::BeginDisabled(!editMode);
            {
                // Optional node name: what CharacterController / AnimGraphInstance::FindNode look up.
                char nameBuffer[64] = {};
                std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", node.Name.c_str());
                if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
                {
                    node.Name = nameBuffer;
                }
                if (ImGui::IsItemActivated())
                {
                    BeginEdit();
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    CommitEdit(sceneEditor, "Rename Node");
                }
            }
            if (type->bHasClipIndex)
            {
                // The card shows what the file says (ClipName); the index is only the legacy fallback.
                // Against the selected model the name either resolves (shown plainly) or is flagged in
                // red: the graph belongs to another skeleton family, or the clip was renamed.
                const int clipCount = clips ? static_cast<int>(clips->size()) : 0;
                const int resolved = clips && !node.ClipName.empty() ? Model::FindClipIndexIn(*clips, node.ClipName) : -1;
                const bool legacyIndex = node.ClipName.empty() && clipCount > 0 && node.ClipIndex >= 0 && node.ClipIndex < clipCount;
                const std::string clipName = !node.ClipName.empty()
                                                     ? node.ClipName
                                                     : (legacyIndex ? (*clips)[static_cast<size_t>(node.ClipIndex)]->GetClipName()
                                                                    : fmt::format("clip {}", node.ClipIndex));
                // Cycling walks the selected model's clip list and writes both the name and the index.
                const int cycleFrom = resolved >= 0 ? resolved : node.ClipIndex;
                if (ImGui::ArrowButton("##clipPrev", ImGuiDir_Left) && clipCount > 0)
                {
                    BeginEdit();
                    node.ClipIndex = ((cycleFrom - 1) % clipCount + clipCount) % clipCount;
                    node.ClipName = (*clips)[static_cast<size_t>(node.ClipIndex)]->GetClipName();
                    CommitEdit(sceneEditor, "Change Clip");
                }
                ImGui::SameLine();
                if (ImGui::ArrowButton("##clipNext", ImGuiDir_Right) && clipCount > 0)
                {
                    BeginEdit();
                    node.ClipIndex = (std::max(cycleFrom, -1) + 1) % clipCount;
                    node.ClipName = (*clips)[static_cast<size_t>(node.ClipIndex)]->GetClipName();
                    CommitEdit(sceneEditor, "Change Clip");
                }
                ImGui::SameLine();
                if (clips != nullptr && resolved < 0 && !legacyIndex)
                {
                    ImGui::TextColored(kErrorColor, "%s", clipName.c_str());
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("not a clip of the selected model (%s)", selected->GetModel()->GetModelFileName().c_str());
                    }
                }
                else
                {
                    ImGui::TextUnformatted(clipName.c_str());
                }
            }
            if (type->bHasParam)
            {
                ImGui::SliderFloat(type->ParamLabel, &node.Param, 0.0f, type->ParamMax, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                if (ImGui::IsItemActivated())
                {
                    BeginEdit();
                }
                if (ImGui::IsItemEdited())
                {
                    SyncAsset(); // live: the running instance follows the drag
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    CommitEdit(sceneEditor, "Change Param");
                }
            }
            if (type->bHasSlot)
            {
                char slotBuffer[64] = {};
                std::snprintf(slotBuffer, sizeof(slotBuffer), "%s", node.SlotName.c_str());
                if (ImGui::InputText("Slot", slotBuffer, sizeof(slotBuffer)))
                {
                    node.SlotName = slotBuffer;
                }
                if (ImGui::IsItemActivated())
                {
                    BeginEdit();
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    CommitEdit(sceneEditor, "Change Slot Name");
                }
            }
            if (type->bHasParam2)
            {
                ImGui::SliderFloat("Speed", &node.Param2, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                if (ImGui::IsItemActivated())
                {
                    BeginEdit();
                }
                if (ImGui::IsItemEdited())
                {
                    SyncAsset();
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    CommitEdit(sceneEditor, "Change Speed");
                }
            }
            if (type->bHasSelect)
            {
                int active = node.ActiveIndex;
                if (ImGui::InputInt("Active", &active))
                {
                    BeginEdit();
                    node.ActiveIndex = std::max(0, active);
                    CommitEdit(sceneEditor, "Change Active Input");
                }
                ImGui::InputFloat("Blend Time", &node.BlendTimeSec, 0.05f, 0.1f, "%.2f");
                if (ImGui::IsItemActivated())
                {
                    BeginEdit();
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    node.BlendTimeSec = std::max(0.0f, node.BlendTimeSec);
                    CommitEdit(sceneEditor, "Change Blend Time");
                }
                // Input names, comma separated (In0, In1, ...): what the state machine selects by.
                std::string joined;
                for (size_t i = 0; i < node.InputNames.size(); ++i)
                {
                    joined += (i ? "," : "") + node.InputNames[i];
                }
                char namesBuffer[256] = {};
                std::snprintf(namesBuffer, sizeof(namesBuffer), "%s", joined.c_str());
                if (ImGui::InputText("Input Names", namesBuffer, sizeof(namesBuffer)))
                {
                    node.InputNames.clear();
                    std::string current;
                    for (const char* p = namesBuffer;; ++p)
                    {
                        if (*p == ',' || *p == 0)
                        {
                            node.InputNames.push_back(current);
                            current.clear();
                            if (*p == 0)
                            {
                                break;
                            }
                        }
                        else if (*p != ' ')
                        {
                            current += *p;
                        }
                    }
                }
                if (ImGui::IsItemActivated())
                {
                    BeginEdit();
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    CommitEdit(sceneEditor, "Change Input Names");
                }
            }
            if (type->bHasMask)
            {
                char maskBuffer[64] = {};
                std::snprintf(maskBuffer, sizeof(maskBuffer), "%s", node.MaskRoot.c_str());
                if (ImGui::InputText("Mask Root", maskBuffer, sizeof(maskBuffer)))
                {
                    node.MaskRoot = maskBuffer;
                }
                if (ImGui::IsItemActivated())
                {
                    BeginEdit();
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    CommitEdit(sceneEditor, "Change Mask Root");
                }
                int depth = node.BlendDepth;
                if (ImGui::InputInt("Blend Depth", &depth))
                {
                    BeginEdit();
                    node.BlendDepth = std::max(0, depth);
                    CommitEdit(sceneEditor, "Change Blend Depth");
                }
            }
            if (type->bHasLoop)
            {
                bool loop = node.bLooping;
                if (ImGui::Checkbox("Loop", &loop))
                {
                    BeginEdit();
                    node.bLooping = loop;
                    CommitEdit(sceneEditor, "Toggle Loop");
                }
            }
            if (type->bHasCacheName)
            {
                char buffer[64];
                std::strncpy(buffer, node.CacheName.c_str(), sizeof(buffer) - 1);
                buffer[sizeof(buffer) - 1] = 0;
                if (ImGui::InputText("Cache", buffer, sizeof(buffer)))
                {
                    if (mEditBefore.Nodes.empty() || !ImGui::IsItemActive())
                    {
                        BeginEdit();
                    }
                    node.CacheName = buffer;
                    SyncAsset();
                }
                if (ImGui::IsItemActivated())
                {
                    BeginEdit();
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    CommitEdit(sceneEditor, "Rename Cache");
                }
            }
            ImGui::EndDisabled();
            ImGui::PopItemWidth();

            // outputs
            for (size_t p = 0; p < type->Outputs.size(); ++p)
            {
                ed::BeginPin(OutputPinId(node.Id, p), ed::PinKind::Output);
                ImGui::Text("%s ->", type->Outputs[p].Name.c_str());
                ed::EndPin();
            }

            ImGui::PopID();
            ed::EndNode();
        }
    }

    void AnimGraphPanel::DrawLinks()
    {
        for (const EditorLink& link : mGraph.Links)
        {
            const EditorNode* from = mGraph.FindNode(link.FromNode);
            const EditorNode* to = mGraph.FindNode(link.ToNode);
            const NodeTypeDesc* fromType = from ? FindNodeType(from->Type) : nullptr;
            const NodeTypeDesc* toType = to ? FindNodeType(to->Type) : nullptr;
            if (fromType == nullptr || toType == nullptr)
            {
                continue;
            }
            const size_t outIdx = PinIndexByName(fromType->Outputs, link.FromPin);
            const size_t inIdx = PinIndexByName(toType->Inputs, link.ToPin);
            const bool seam = bShowGpuOverlay && mSeamSlotOf.count(link.Id) > 0;
            ed::Link(LinkIdOf(link.Id), OutputPinId(link.FromNode, outIdx), InputPinId(link.ToNode, inIdx), seam ? kSeamColor : kLinkColor, seam ? 3.0f : 1.5f);
        }
    }

    void AnimGraphPanel::HandleCreation(SceneEditor& sceneEditor, bool editMode)
    {
        if (!editMode)
        {
            return;
        }
        if (ed::BeginCreate(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 2.0f))
        {
            ed::PinId a, b;
            if (ed::QueryNewLink(&a, &b))
            {
                DecodedPin pa = DecodePin(a);
                DecodedPin pb = DecodePin(b);
                if (!pa.valid || !pb.valid || pa.output == pb.output)
                {
                    mStatusMessage = "connect an output (right) to an input (left)";
                    mStatusIsError = true;
                    ed::RejectNewItem(kErrorColor, 2.0f);
                }
                else
                {
                    const DecodedPin& out = pa.output ? pa : pb;
                    const DecodedPin& in = pa.output ? pb : pa;
                    const std::string fromPin = PinName(mGraph, out);
                    const std::string toPin = PinName(mGraph, in);
                    const EditorGraph::LinkCheck check = mGraph.CanCreateLink(out.nodeId, fromPin, in.nodeId, toPin);
                    if (!check.bOk)
                    {
                        mStatusMessage = check.Reason;
                        mStatusIsError = true;
                        ed::RejectNewItem(kErrorColor, 2.0f);
                    }
                    else if (ed::AcceptNewItem(kGpuColor, 2.0f))
                    {
                        BeginEdit();
                        mGraph.AddLink(out.nodeId, fromPin, in.nodeId, toPin);
                        CommitEdit(sceneEditor, "Connect");
                        mStatusMessage = fmt::format("connected {} -> {}", fromPin, toPin);
                        mStatusIsError = false;
                    }
                }
            }
        }
        ed::EndCreate();
    }

    void AnimGraphPanel::HandleDeletion(SceneEditor& sceneEditor, bool editMode)
    {
        if (ed::BeginDelete())
        {
            ed::LinkId linkId;
            while (ed::QueryDeletedLink(&linkId))
            {
                if (!editMode)
                {
                    ed::RejectDeletedItem();
                    continue;
                }
                if (ed::AcceptDeletedItem())
                {
                    BeginEdit();
                    mGraph.RemoveLink(static_cast<int>(linkId.Get() - kLinkBase));
                    CommitEdit(sceneEditor, "Disconnect");
                }
            }
            ed::NodeId nodeId;
            while (ed::QueryDeletedNode(&nodeId))
            {
                if (!editMode)
                {
                    ed::RejectDeletedItem();
                    continue;
                }
                if (ed::AcceptDeletedItem())
                {
                    BeginEdit();
                    mGraph.RemoveNode(static_cast<int>(nodeId.Get()));
                    CommitEdit(sceneEditor, "Delete Node");
                }
            }
        }
        ed::EndDelete();
    }

    void AnimGraphPanel::Draw(SceneEditor& sceneEditor)
    {
        // Dialogs must be pumped every frame regardless of the window state.
        HandleDialogs(sceneEditor);

        if (!bVisible || mContext == nullptr)
        {
            return;
        }
        ImGui::SetNextWindowSize(ImVec2(900.0f, 560.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("AnimGraph Editor", &bVisible))
        {
            ImGui::End();
            return;
        }

        ModelAndInstanceData& scene = sceneEditor.ModelData();
        ModelInstance* selected = nullptr;
        if (!scene.miModelInstances.empty())
        {
            selected = scene.miModelInstances[std::clamp(scene.miSelectedInstance, 0, static_cast<int>(scene.miModelInstances.size()) - 1)].get();
        }
        const bool editMode = sceneEditor.IsEditMode();
        FollowModelFamily(selected != nullptr ? selected->GetModel()->GetAssetFamily() : std::string());

        DrawToolbar(sceneEditor);
        if (!mStatusMessage.empty())
        {
            ImGui::TextColored(mStatusIsError ? kErrorColor : ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "%s", mStatusMessage.c_str());
        }
        ImGui::Separator();

        ed::SetCurrentEditor(mContext);
        ed::Begin("AnimGraphCanvas", ImVec2(0.0f, 0.0f));

        if (!bPositionsRestored)
        {
            for (const EditorNode& node : mGraph.Nodes)
            {
                ed::SetNodePosition(ed::NodeId(static_cast<uintptr_t>(node.Id)), ImVec2(node.PosX, node.PosY));
            }
            bPositionsRestored = true;
        }
        if (mPendingPlacement >= 0)
        {
            if (const EditorNode* node = mGraph.FindNode(mPendingPlacement))
            {
                ed::SetNodePosition(ed::NodeId(static_cast<uintptr_t>(node->Id)), ImVec2(node->PosX, node->PosY));
            }
            mPendingPlacement = -1;
        }

        DrawNodes(sceneEditor, selected);
        DrawLinks();
        HandleCreation(sceneEditor, editMode);
        HandleDeletion(sceneEditor, editMode);

        // Read positions back so Save/Apply persist where the user left the nodes.
        for (EditorNode& node : mGraph.Nodes)
        {
            const ImVec2 pos = ed::GetNodePosition(ed::NodeId(static_cast<uintptr_t>(node.Id)));
            node.PosX = pos.x;
            node.PosY = pos.y;
        }

        ed::End();
        ed::SetCurrentEditor(nullptr);
        ImGui::End();
    }
} // namespace RAnimation
