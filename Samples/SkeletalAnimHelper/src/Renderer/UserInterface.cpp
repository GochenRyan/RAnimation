#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <ImGuiFileDialog.h>

#include <Model\InstanceSettings.h>
#include <Model\Model.h>
#include <Model\ModelInstance.h>
#include <Model\UsdSceneExporter.h>
#include <Editor/SceneEditor.h>
#include <Renderer/UserInterface.h>

using namespace RAnimation;

namespace
{
    const char* GetRenderResourceTierName(RenderResourceTier tier)
    {
        switch (tier)
        {
            case RenderResourceTier::Unsupported:
                return "Unsupported";
            case RenderResourceTier::Low:
                return "Low";
            case RenderResourceTier::Medium:
                return "Medium";
            case RenderResourceTier::High:
                return "High";
            default:
                return "Unknown";
        }
    }

    std::string FormatBytes(uint64_t bytes)
    {
        const char* unit = "B";
        double value = static_cast<double>(bytes);
        if (value >= 1024.0 * 1024.0)
        {
            value /= 1024.0 * 1024.0;
            unit = "MB";
        }
        else if (value >= 1024.0)
        {
            value /= 1024.0;
            unit = "KB";
        }

        char buffer[64] = {};
        std::snprintf(buffer, sizeof(buffer), "%.2f %s", value, unit);
        return buffer;
    }

    float GetUsageRatio(uint64_t used, uint64_t limit)
    {
        if (limit == 0)
        {
            return 0.0f;
        }

        return std::clamp(static_cast<float>(static_cast<double>(used) / static_cast<double>(limit)), 0.0f, 1.0f);
    }

    void DrawBudgetRow(const char* label, uint64_t used, uint64_t limit)
    {
        ImGui::Text("%-20s %8llu / %-8llu", label, static_cast<unsigned long long>(used), static_cast<unsigned long long>(limit));
        ImGui::ProgressBar(GetUsageRatio(used, limit), ImVec2(-1.0f, 0.0f));
    }

    void PushRollingValue(std::vector<float>& values, int& offset, int maxNum, float value)
    {
        if (values.empty())
        {
            values.resize(maxNum, 0.0f);
        }

        values[offset] = value;
        offset = (offset + 1) % maxNum;
    }
} // namespace

bool UserInterface::Init(RRenderData& renderData)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendRendererName = "NRI";

    ImGui::StyleColorsDark();

    if (renderData.rdSDLWindow == nullptr)
    {
        return false;
    }

    if (!ImGui_ImplSDL3_InitForOther(renderData.rdSDLWindow))
    {
        return false;
    }

    if (!renderData.NRI.HasImgui())
    {
        return false;
    }

    nri::ImguiDesc imguiDesc = {};
    imguiDesc.descriptorPoolSize = 16384;
    NRI_ABORT_ON_FAILURE(renderData.NRI.CreateImgui(*renderData.rdDevice, imguiDesc, renderData.rdImgui));

    mFPSValues.resize(mNumFPSValues, 0.0f);
    mFrameTimeValues.resize(mNumFrameTimeValues, 0.0f);
    mModelUploadValues.resize(mNumModelUploadValues, 0.0f);
    mMatrixGenerationValues.resize(mNumMatrixGenerationValues, 0.0f);
    mMatrixUploadValues.resize(mNumMatrixUploadValues, 0.0f);
    mUiGenValues.resize(mNumUiGenValues, 0.0f);
    mUiDrawValues.resize(mNumUiDrawValues, 0.0f);

    return true;
}

void UserInterface::HideMouse(bool hide)
{
    ImGuiIO& io = ImGui::GetIO();
    if (hide)
    {
        io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    }
    else
    {
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }
}

void UserInterface::drawMenuBarAndHotkeys(RRenderData& renderData, SceneEditor& sceneEditor)
{
    ImGuiIO& io = ImGui::GetIO();
    const bool editMode = sceneEditor.IsEditMode();

    // Editor hotkeys. Ignore while a text field has focus (e.g. the import file dialog) so typing
    // does not steal Ctrl+Z/Y. Undo/redo only act in Edit mode; mode toggle works in either.
    if (!io.WantTextInput)
    {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_E))
        {
            sceneEditor.ToggleMode();
        }
        if (editMode)
        {
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
            {
                sceneEditor.Undo();
            }
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) ||
                ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
            {
                sceneEditor.Redo();
            }
        }
    }

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            // Importing mutates the scene (adds models/instances), so it needs Edit mode like Import Model.
            if (ImGui::MenuItem("Import Scene from USD...", nullptr, false, editMode))
            {
                IGFD::FileDialogConfig config;
                config.path = ".";
                config.countSelectionMax = 1;
                config.flags = ImGuiFileDialogFlags_Modal;
                ImGuiFileDialog::Instance()->OpenDialog(
                        "ImportSceneFromUSD", "Import Scene from USD", "USD Scene{.usda,.usd,.usdc}", config);
            }

            if (ImGui::MenuItem("Export Scene to USD..."))
            {
                IGFD::FileDialogConfig config;
                config.path = ".";
                config.fileName = "untitled.level.usda";
                config.countSelectionMax = 1;
                config.flags = ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_ConfirmOverwrite;
                ImGuiFileDialog::Instance()->OpenDialog(
                        "ExportSceneToUSD", "Export Scene to USD", "USD Scene{.usda}", config);
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit"))
        {
            const std::string undoLabel = std::string("Undo ") + sceneEditor.UndoName();
            if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, editMode && sceneEditor.CanUndo()))
            {
                sceneEditor.Undo();
            }

            const std::string redoLabel = std::string("Redo ") + sceneEditor.RedoName();
            if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, editMode && sceneEditor.CanRedo()))
            {
                sceneEditor.Redo();
            }

            ImGui::Separator();

            bool editFlag = editMode;
            if (ImGui::MenuItem("Edit Mode", "Ctrl+E", &editFlag))
            {
                sceneEditor.ToggleMode();
            }

            ImGui::EndMenu();
        }

        const char* modeText = editMode ? "[ EDIT ]" : "[ VIEW ]";
        ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(modeText).x - 20.0f);
        ImGui::TextUnformatted(modeText);

        ImGui::EndMainMenuBar();
    }

    // Resolve the export dialog (opened from the File menu). Pure CPU/USD work, no Renderer involvement.
    if (ImGuiFileDialog::Instance()->Display("ExportSceneToUSD"))
    {
        if (ImGuiFileDialog::Instance()->IsOk())
        {
            const std::string outPath = ImGuiFileDialog::Instance()->GetFilePathName();
            ExportSceneToUsd(sceneEditor.ModelData(), renderData.rdCameraRig, outPath);
        }
        ImGuiFileDialog::Instance()->Close();
    }

    // Resolve the import dialog: parse the scene USD, then load each asset and recreate its instances.
    if (ImGuiFileDialog::Instance()->Display("ImportSceneFromUSD"))
    {
        if (ImGuiFileDialog::Instance()->IsOk())
        {
            ModelAndInstanceData& modInstData = sceneEditor.ModelData();
            const std::string scenePath = ImGuiFileDialog::Instance()->GetFilePathName();

            std::vector<ImportedSceneInstance> imported;
            CameraRig importedRig;
            if (ImportSceneFromUsd(scenePath, imported, importedRig))
            {
                renderData.rdCameraRig = importedRig;

                for (const ImportedSceneInstance& imp : imported)
                {
                    std::shared_ptr<Model> model = sceneEditor.GetModel(imp.assetPath);
                    if (model == nullptr)
                    {
                        // first instance of this asset: AddModel loads it and creates one default instance,
                        // which we reconfigure with this instance's settings
                        if (!sceneEditor.AddModel(imp.assetPath))
                        {
                            continue;
                        }
                        if (!modInstData.miModelInstances.empty())
                        {
                            modInstData.miModelInstances.back()->SetInstanceSettings(imp.settings);
                        }
                    }
                    else
                    {
                        // further instances of an already-loaded asset
                        std::shared_ptr<ModelInstance> instance = sceneEditor.AddInstance(model);
                        if (instance != nullptr)
                        {
                            instance->SetInstanceSettings(imp.settings);
                        }
                    }
                }

                modInstData.miSelectedModel = std::max(0, static_cast<int>(modInstData.miModelList.size()) - 1);
                modInstData.miSelectedInstance =
                        std::max(0, static_cast<int>(modInstData.miModelInstances.size()) - 1);
            }
        }
        ImGuiFileDialog::Instance()->Close();
    }
}

void UserInterface::trackSettingsEdit(SceneEditor& sceneEditor,
                                      const std::shared_ptr<ModelInstance>& instance,
                                      const InstanceSettings& edited,
                                      const char* editName)
{
    // Coalesce a continuous slider drag into a single command: snapshot the pre-edit settings when
    // the widget becomes active (the instance still holds the previous frame's applied value), push
    // one command (before -> final) when the edit finishes.
    if (ImGui::IsItemActivated())
    {
        mPendingSettingsInstance = instance;
        mPendingSettingsBefore = instance->GetInstanceSettings();
    }

    if (ImGui::IsItemDeactivatedAfterEdit() && mPendingSettingsInstance == instance)
    {
        sceneEditor.RecordInstanceEdit(instance, mPendingSettingsBefore, edited, editName);
        mPendingSettingsInstance = nullptr;
    }
}

void UserInterface::CreateFrame(RRenderData& renderData, SceneEditor& sceneEditor)
{
    mUIGenerateTimer.Start();

    ModelAndInstanceData& modInstData = sceneEditor.ModelData();

    HideMouse(false);

    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

    drawMenuBarAndHotkeys(renderData, sceneEditor);

    const bool editMode = sceneEditor.IsEditMode();

    // Viewport left-click (not over an ImGui window) requests a GPU pick. The Renderer issues the
    // 1px ID readback next frame and resolves the selection. MousePos is in points; scale to pixels.
    {
        ImGuiIO& io = ImGui::GetIO();
        if (io.MouseClicked[0] && !io.WantCaptureMouse)
        {
            const int pixelX = static_cast<int>(io.MousePos.x * io.DisplayFramebufferScale.x);
            const int pixelY = static_cast<int>(io.MousePos.y * io.DisplayFramebufferScale.y);
            renderData.rdPendingPick.x =
                    std::clamp(pixelX, 0, static_cast<int>(renderData.rdOutputResolution.x) - 1);
            renderData.rdPendingPick.y =
                    std::clamp(pixelY, 0, static_cast<int>(renderData.rdOutputResolution.y) - 1);
            renderData.rdPendingPick.requested = true;
        }
    }

    // Camera hotkeys: Alt+1..4 select the camera slot, P toggles projection, Alt+R resets the active
    // camera. Gated on !WantTextInput so typing in the file dialog doesn't trigger them.
    {
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput)
        {
            CameraRig& rig = renderData.rdCameraRig;
            if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_1))
            {
                rig.active = 0;
            }
            if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_2))
            {
                rig.active = 1;
            }
            if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_3))
            {
                rig.active = 2;
            }
            if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_4))
            {
                rig.active = 3;
            }
            rig.active = std::clamp(rig.active, 0, 3);

            if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_R))
            {
                rig.ResetActive();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_P, false))
            {
                CameraCommon& c = rig.ActiveCommon();
                c.projection = (c.projection == ProjectionType::Perspective) ? ProjectionType::Orthographic
                                                                             : ProjectionType::Perspective;
            }
        }
    }

    if (renderData.rdFrameTime > 0.0f)
    {
        mNewFps = 1000.0f / renderData.rdFrameTime;
    }
    mFramesPerSecond = mAveragingAlpha * mFramesPerSecond + (1.0f - mAveragingAlpha) * mNewFps;

    if (mUpdateTime < 0.000001)
    {
        mUpdateTime = ImGui::GetTime();
    }

    while (mUpdateTime < ImGui::GetTime())
    {
        PushRollingValue(mFPSValues, mFpsOffset, mNumFPSValues, mFramesPerSecond);
        PushRollingValue(mFrameTimeValues, mFrameTimeOffset, mNumFrameTimeValues, renderData.rdFrameTime);
        PushRollingValue(mModelUploadValues, mModelUploadOffset, mNumModelUploadValues, renderData.rdUploadToVBOTime);
        PushRollingValue(
                mMatrixGenerationValues, mMatrixGenOffset, mNumMatrixGenerationValues, renderData.rdMatrixGenerateTime);
        PushRollingValue(
                mMatrixUploadValues, mMatrixUploadOffset, mNumMatrixUploadValues, renderData.rdUploadToUBOTime);
        PushRollingValue(mUiGenValues, mUiGenOffset, mNumUiGenValues, renderData.rdUIGenerateTime);
        PushRollingValue(mUiDrawValues, mUiDrawOffset, mNumUiDrawValues, renderData.rdUIDrawTime);
        mUpdateTime += 1.0 / 30.0;
    }

    modInstData.miSelectedModel = std::clamp(modInstData.miSelectedModel,
                                             0,
                                             std::max(0, static_cast<int>(modInstData.miModelList.size()) - 1));
    modInstData.miSelectedInstance = std::clamp(modInstData.miSelectedInstance,
                                                0,
                                                std::max(0, static_cast<int>(modInstData.miModelInstances.size()) - 1));

    ImGuiSliderFlags sliderFlags = ImGuiSliderFlags_AlwaysClamp;

    if (!ImGui::Begin("Control"))
    {
        ImGui::End();
        return;
    }

    ImGui::Text("FPS: %10.4f", mFramesPerSecond);

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        float averageFPS = 0.0f;
        for (const auto value : mFPSValues) {
        averageFPS += value;
        }
        averageFPS /= static_cast<float>(mNumFPSValues);
        std::string fpsOverlay = "now:     " + std::to_string(mFramesPerSecond) + "\n30s avg: " + std::to_string(averageFPS);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("FPS");
        ImGui::SameLine();
        ImGui::PlotLines("##FrameTimes", mFPSValues.data(), mFPSValues.size(), mFpsOffset, fpsOverlay.c_str(), 0.0f,
                        std::numeric_limits<float>::max(), ImVec2(0, 80));
        ImGui::EndTooltip();
    }

    if (ImGui::CollapsingHeader("Info", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Triangles:              %10i", renderData.rdTriangleCount);

        std::string unit = "B";
        float memoryUsage = renderData.rdMatricesSize;

        if (memoryUsage > 1024.0f * 1024.0f) {
        memoryUsage /= 1024.0f * 1024.0f;
        unit = "MB";
        } else  if (memoryUsage > 1024.0f) {
        memoryUsage /= 1024.0f;
        unit = "KB";
        }

        ImGui::Text("Instance Matrix Size:  %8.2f %2s", memoryUsage, unit.c_str());

        std::string windowDims = std::to_string(renderData.rdOutputResolution.x) + "x" + std::to_string(renderData.rdOutputResolution.y);
        ImGui::Text("Window Dimensions:      %10s", windowDims.c_str());

        std::string imgWindowPos = std::to_string(static_cast<int>(ImGui::GetWindowPos().x)) + "/" + std::to_string(static_cast<int>(ImGui::GetWindowPos().y));
        ImGui::Text("ImGui Window Position:  %10s", imgWindowPos.c_str());
    }

    if (ImGui::CollapsingHeader("Budget", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const RenderResourceBudget& budget = renderData.rdResourceBudget;
        const RenderResourceBudgetUsage& usage = renderData.rdResourceBudgetUsage;

        ImGui::Text("Tier:                  %10s", GetRenderResourceTierName(budget.tier));
        ImGui::Text("Animation Budget:      %10s", FormatBytes(budget.animationMemoryBudget).c_str());
        ImGui::Text("Estimated Max Total:   %10s", FormatBytes(budget.estimatedAnimationBufferBytesTotal).c_str());
        ImGui::Text("Current Upload:        %10s", FormatBytes(usage.uploadBytes).c_str());
        ImGui::Separator();

        DrawBudgetRow("Static World", usage.staticWorldMatrices, budget.maxWorldMatrices);
        DrawBudgetRow("Animated Instances", usage.animatedInstances, budget.maxWorldMatrices);
        DrawBudgetRow("Bone Matrices", usage.boneMatrices, budget.GetMaxBoneMatrices());
        DrawBudgetRow("Node Transforms", usage.nodeTransforms, budget.GetMaxNodeTransforms());
    }

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
    {
        CameraRig& rig = renderData.rdCameraRig;
        rig.active = std::clamp(rig.active, 0, 3);

        static const char* kCameraNames[] = {"1: Free", "2: FirstPerson", "3: ThirdPerson", "4: Stationary"};
        if (ImGui::BeginCombo("Active Camera", kCameraNames[rig.active]))
        {
            for (int i = 0; i < 4; ++i)
            {
                const bool selected = (rig.active == i);
                if (ImGui::Selectable(kCameraNames[i], selected))
                {
                    rig.active = i;
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        CameraCommon& c = rig.ActiveCommon();
        static const char* kProjNames[] = {"Perspective", "Orthographic"};
        int proj = static_cast<int>(c.projection);
        if (ImGui::Combo("Projection", &proj, kProjNames, IM_ARRAYSIZE(kProjNames)))
        {
            c.projection = static_cast<ProjectionType>(proj);
        }

        if (ImGui::Button("Reset Camera"))
        {
            rig.ResetActive();
        }

        ImGui::Separator();

        // Shared projection parameter (FOV or ortho extent) then the type-specific controls.
        if (c.projection == ProjectionType::Perspective)
        {
            ImGui::SliderFloat("Field of View", &c.fovDeg, 40.0f, 150.0f, "%.1f", sliderFlags);
        }
        else
        {
            ImGui::SliderFloat("Ortho Half-Height", &c.orthoHalfHeight, 0.5f, 50.0f, "%.2f", sliderFlags);
        }

        switch (rig.ActiveType())
        {
            case CameraType::Free:
            {
                FreeCamera& cam = rig.free;
                ImGui::Text("Position: %s", glm::to_string(cam.position).c_str());
                ImGui::SliderFloat3("Position", glm::value_ptr(cam.position), -50.0f, 50.0f, "%.2f", sliderFlags);
                ImGui::SliderFloat("Yaw", &cam.yawDeg, 0.0f, 360.0f, "%.1f", sliderFlags);
                ImGui::SliderFloat("Pitch", &cam.pitchDeg, -89.0f, 89.0f, "%.1f", sliderFlags);
                ImGui::SliderFloat("Move Speed", &cam.moveSpeed, 0.5f, 50.0f, "%.1f", sliderFlags);
                ImGui::SliderFloat("Mouse Sensitivity", &cam.mouseSensitivity, 0.01f, 1.0f, "%.3f", sliderFlags);
                break;
            }

            case CameraType::FirstPerson:
            {
                FirstPersonCamera& cam = rig.firstPerson;
                char headBuf[128];
                std::snprintf(headBuf, sizeof(headBuf), "%s", cam.headBoneName.c_str());
                if (ImGui::InputText("Head Bone", headBuf, sizeof(headBuf)))
                {
                    cam.headBoneName = headBuf;
                }
                ImGui::SliderFloat3("Eye Offset", glm::value_ptr(cam.eyeOffset), -1.0f, 1.0f, "%.3f", sliderFlags);
                ImGui::SliderFloat("Forward Push", &cam.forwardPush, 0.0f, 2.0f, "%.3f", sliderFlags);
                ImGui::SliderFloat("Yaw Offset", &cam.yawDeg, 0.0f, 360.0f, "%.1f", sliderFlags);
                ImGui::SliderFloat("Pitch Offset", &cam.pitchDeg, -89.0f, 89.0f, "%.1f", sliderFlags);
                ImGui::SliderFloat("Roll", &cam.rollDeg, 0.0f, 360.0f, "%.1f", sliderFlags);
                ImGui::SliderFloat("Mouse Sensitivity", &cam.mouseSensitivity, 0.01f, 1.0f, "%.3f", sliderFlags);
                break;
            }

            case CameraType::ThirdPerson:
            {
                ThirdPersonCamera& cam = rig.thirdPerson;
                char headBuf[128];
                std::snprintf(headBuf, sizeof(headBuf), "%s", cam.headBoneName.c_str());
                if (ImGui::InputText("Head Bone", headBuf, sizeof(headBuf)))
                {
                    cam.headBoneName = headBuf;
                }
                ImGui::SliderFloat("Distance", &cam.distance, 0.5f, 50.0f, "%.2f", sliderFlags);
                // ThirdPerson yaw/pitch are really an orbit (azimuth/elevation) - annotate the true meaning.
                ImGui::SliderFloat("Yaw (azimuth)", &cam.yawDeg, 0.0f, 360.0f, "%.1f", sliderFlags);
                ImGui::SliderFloat("Pitch (elevation)", &cam.pitchDeg, -89.0f, 89.0f, "%.1f", sliderFlags);
                ImGui::SliderFloat("Damping", &cam.damping, 0.5f, 30.0f, "%.1f", sliderFlags);
                ImGui::SliderFloat("Mouse Sensitivity", &cam.mouseSensitivity, 0.01f, 1.0f, "%.3f", sliderFlags);
                break;
            }

            case CameraType::Stationary:
            {
                StationaryCamera& cam = rig.stationary;
                ImGui::Text("Position: %s", glm::to_string(cam.position).c_str());
                ImGui::SliderFloat3("Position", glm::value_ptr(cam.position), -50.0f, 50.0f, "%.2f", sliderFlags);
                ImGui::SliderFloat("Damping", &cam.damping, 0.5f, 30.0f, "%.1f", sliderFlags);
                break;
            }
        }

        ImGui::Separator();
        ImGui::TextDisabled("Alt+1..4 switch | P projection | Alt+R reset | RMB look");
    }

    if (ImGui::CollapsingHeader("Timers", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Frame Time:             %10.4f ms", renderData.rdFrameTime);

        if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        float averageFrameTime = 0.0f;
        for (const auto value : mFrameTimeValues) {
            averageFrameTime += value;
        }
        averageFrameTime /= static_cast<float>(mNumMatrixGenerationValues);
        std::string frameTimeOverlay = "now:     " + std::to_string(renderData.rdFrameTime) +
            " ms\n30s avg: " + std::to_string(averageFrameTime) + " ms";
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Frame Time       ");
        ImGui::SameLine();
        ImGui::PlotLines("##FrameTime", mFrameTimeValues.data(), mFrameTimeValues.size(), mFrameTimeOffset,
            frameTimeOverlay.c_str(), 0.0f, std::numeric_limits<float>::max(), ImVec2(0, 80));
        ImGui::EndTooltip();
        }

        ImGui::Text("Model Upload Time:      %10.4f ms", renderData.rdUploadToVBOTime);

        if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        float averageModelUpload = 0.0f;
        for (const auto value : mModelUploadValues) {
            averageModelUpload += value;
        }
        averageModelUpload /= static_cast<float>(mNumModelUploadValues);
        std::string modelUploadOverlay = "now:     " + std::to_string(renderData.rdUploadToVBOTime) +
            " ms\n30s avg: " + std::to_string(averageModelUpload) + " ms";
        ImGui::AlignTextToFramePadding();
        ImGui::Text("VBO Upload");
        ImGui::SameLine();
        ImGui::PlotLines("##ModelUploadTimes", mModelUploadValues.data(), mModelUploadValues.size(), mModelUploadOffset,
            modelUploadOverlay.c_str(), 0.0f, std::numeric_limits<float>::max(), ImVec2(0, 80));
        ImGui::EndTooltip();
        }

        ImGui::Text("Matrix Generation Time: %10.4f ms", renderData.rdMatrixGenerateTime);

        if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        float averageMatGen = 0.0f;
        for (const auto value : mMatrixGenerationValues) {
            averageMatGen += value;
        }
        averageMatGen /= static_cast<float>(mNumMatrixGenerationValues);
        std::string matrixGenOverlay = "now:     " + std::to_string(renderData.rdMatrixGenerateTime) +
            " ms\n30s avg: " + std::to_string(averageMatGen) + " ms";
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Matrix Generation");
        ImGui::SameLine();
        ImGui::PlotLines("##MatrixGenTimes", mMatrixGenerationValues.data(), mMatrixGenerationValues.size(), mMatrixGenOffset,
            matrixGenOverlay.c_str(), 0.0f, std::numeric_limits<float>::max(), ImVec2(0, 80));
        ImGui::EndTooltip();
        }

        ImGui::Text("Matrix Upload Time:     %10.4f ms", renderData.rdUploadToUBOTime);

        if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        float averageMatrixUpload = 0.0f;
        for (const auto value : mMatrixUploadValues) {
            averageMatrixUpload += value;
        }
        averageMatrixUpload /= static_cast<float>(mNumMatrixUploadValues);
        std::string matrixUploadOverlay = "now:     " + std::to_string(renderData.rdUploadToUBOTime) +
            " ms\n30s avg: " + std::to_string(averageMatrixUpload) + " ms";
        ImGui::AlignTextToFramePadding();
        ImGui::Text("UBO Upload");
        ImGui::SameLine();
        ImGui::PlotLines("##MatrixUploadTimes", mMatrixUploadValues.data(), mMatrixUploadValues.size(), mMatrixUploadOffset,
            matrixUploadOverlay.c_str(), 0.0f, std::numeric_limits<float>::max(), ImVec2(0, 80));
        ImGui::EndTooltip();
        }

        ImGui::Text("UI Generation Time:     %10.4f ms", renderData.rdUIGenerateTime);

        if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        float averageUiGen = 0.0f;
        for (const auto value : mUiGenValues) {
            averageUiGen += value;
        }
        averageUiGen /= static_cast<float>(mNumUiGenValues);
        std::string uiGenOverlay = "now:     " + std::to_string(renderData.rdUIGenerateTime) +
            " ms\n30s avg: " + std::to_string(averageUiGen) + " ms";
        ImGui::AlignTextToFramePadding();
        ImGui::Text("UI Generation");
        ImGui::SameLine();
        ImGui::PlotLines("##UIGenTimes", mUiGenValues.data(), mUiGenValues.size(), mUiGenOffset,
            uiGenOverlay.c_str(), 0.0f, std::numeric_limits<float>::max(), ImVec2(0, 80));
        ImGui::EndTooltip();
        }

        ImGui::Text("UI Draw Time:           %10.4f ms", renderData.rdUIDrawTime);

        if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        float averageUiDraw = 0.0f;
        for (const auto value : mUiDrawValues) {
            averageUiDraw += value;
        }
        averageUiDraw /= static_cast<float>(mNumUiDrawValues);
        std::string uiDrawOverlay = "now:     " + std::to_string(renderData.rdUIDrawTime) +
            " ms\n30s avg: " + std::to_string(averageUiDraw) + " ms";
        ImGui::AlignTextToFramePadding();
        ImGui::Text("UI Draw");
        ImGui::SameLine();
        ImGui::PlotLines("##UIDrawTimes", mUiDrawValues.data(), mUiDrawValues.size(), mUiDrawOffset,
            uiDrawOverlay.c_str(), 0.0f, std::numeric_limits<float>::max(), ImVec2(0, 80));
        ImGui::EndTooltip();
        }
    }

    if (ImGui::CollapsingHeader("Models", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::BeginDisabled(!editMode);
        if (ImGui::Button("Import Model"))
        {
            IGFD::FileDialogConfig config;
            config.path = ".";
            config.countSelectionMax = 1;
            config.flags = ImGuiFileDialogFlags_Modal;
            ImGuiFileDialog::Instance()->OpenDialog("ChooseModelFile",
                                                    "Choose USD Asset",
                                                    "USD Assets{.usd,.usda,.usdc,.usdz}",
                                                    config);
        }

        if (ImGuiFileDialog::Instance()->Display("ChooseModelFile"))
        {
            if (ImGuiFileDialog::Instance()->IsOk())
            {
                std::string filePathName = ImGuiFileDialog::Instance()->GetFilePathName();
                std::filesystem::path currentPath = std::filesystem::current_path();
                std::string relativePath = std::filesystem::relative(filePathName, currentPath).generic_string();
                if (!relativePath.empty())
                {
                    filePathName = relativePath;
                }
                std::replace(filePathName.begin(), filePathName.end(), '\\', '/');

                sceneEditor.AddModel(filePathName);
                modInstData.miSelectedModel = std::max(0, static_cast<int>(modInstData.miModelList.size()) - 1);
                modInstData.miSelectedInstance =
                        std::max(0, static_cast<int>(modInstData.miModelInstances.size()) - 1);
            }

            ImGuiFileDialog::Instance()->Close();
        }
        ImGui::EndDisabled();

        if (!modInstData.miModelList.empty())
        {
            std::string selectedModelName = modInstData.miModelList[modInstData.miSelectedModel]->GetModelFileName();
            if (ImGui::BeginCombo("Model", selectedModelName.data()))
            {
                for (int i = 0; i < static_cast<int>(modInstData.miModelList.size()); ++i)
                {
                    const bool isSelected = modInstData.miSelectedModel == i;
                    if (ImGui::Selectable(selectedModelName.data(), isSelected))
                    {
                        modInstData.miSelectedModel = i;
                    }

                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::BeginDisabled(!editMode);
            if (ImGui::Button("Delete Model"))
            {
                sceneEditor.DeleteModel(
                        modInstData.miModelList[modInstData.miSelectedModel]->GetModelFileNamePath());
                modInstData.miSelectedModel =
                        std::clamp(modInstData.miSelectedModel - 1,
                                   0,
                                   std::max(0, static_cast<int>(modInstData.miModelList.size()) - 1));
                modInstData.miSelectedInstance =
                        std::clamp(modInstData.miSelectedInstance,
                                   0,
                                   std::max(0, static_cast<int>(modInstData.miModelInstances.size()) - 1));

                if (modInstData.miModelList.empty())
                {
                    ImGui::EndDisabled();
                    ImGui::End();
                    return;
                }
            }
            ImGui::EndDisabled();
        }
    }

    if (ImGui::CollapsingHeader("Instances", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Instance Count: %d", static_cast<int>(modInstData.miModelInstances.size()));

        if (!modInstData.miModelInstances.empty())
        {
            ImGui::SliderInt("Selected Instance",
                             &modInstData.miSelectedInstance,
                             0,
                             static_cast<int>(modInstData.miModelInstances.size()) - 1,
                             "%d",
                             sliderFlags);

            std::shared_ptr<ModelInstance> instance = modInstData.miModelInstances[modInstData.miSelectedInstance];
            InstanceSettings settings = instance->GetInstanceSettings();

            ImGui::BeginDisabled(!editMode);
            if (ImGui::Button("Clone Instance"))
            {
                sceneEditor.CloneInstance(instance);
                modInstData.miSelectedInstance = std::max(0, static_cast<int>(modInstData.miModelInstances.size()) - 1);
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            const std::string modelName = instance->GetModel()->GetModelFileName();
            const size_t perModelCount = modInstData.miModelInstancesPerModel[modelName].size();
            ImGui::BeginDisabled(!editMode || perModelCount < 2);
            if (ImGui::Button("Delete Instance"))
            {
                sceneEditor.DeleteInstance(instance);
                modInstData.miSelectedInstance =
                        std::clamp(modInstData.miSelectedInstance - 1,
                                   0,
                                   std::max(0, static_cast<int>(modInstData.miModelInstances.size()) - 1));

                if (modInstData.miModelInstances.empty())
                {
                    ImGui::EndDisabled();
                    ImGui::End();
                    return;
                }
            }
            ImGui::EndDisabled();

            // Camera focus is a viewing aid, available in both modes.
            if (ImGui::Button("Center on Instance"))
            {
                sceneEditor.FocusCameraOn(instance);
            }

            ImGui::BeginDisabled(!editMode);

            ImGui::SliderFloat3(
                    "World Position", glm::value_ptr(settings.mWorldPosition), -25.0f, 25.0f, "%.2f", sliderFlags);
            trackSettingsEdit(sceneEditor, instance, settings, "Move Instance");
            ImGui::SliderFloat3(
                    "World Rotation", glm::value_ptr(settings.mWorldRotation), -180.0f, 180.0f, "%.1f", sliderFlags);
            trackSettingsEdit(sceneEditor, instance, settings, "Rotate Instance");
            ImGui::SliderFloat("Scale", &settings.mScale, 0.01f, 10.0f, "%.3f", sliderFlags);
            trackSettingsEdit(sceneEditor, instance, settings, "Scale Instance");

            instance->SetInstanceSettings(settings);

            if (ImGui::Button("Create Multiple Instances")) {
                std::shared_ptr<Model> currentModel = modInstData.miModelList[modInstData.miSelectedModel];
                sceneEditor.AddInstances(currentModel, mManyInstanceCreateNum);
                modInstData.miSelectedInstance = modInstData.miModelInstances.size() - 1;
            }
            ImGui::SameLine();
            ImGui::SliderInt("##MassInstanceCreation", &mManyInstanceCreateNum, 1, 100, "%d", ImGuiSliderFlags_AlwaysClamp);

            ImGui::EndDisabled();
        }
    }

    if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (!modInstData.miModelInstances.empty())
        {
            std::shared_ptr<ModelInstance> instance = modInstData.miModelInstances[modInstData.miSelectedInstance];
            const auto& clips = instance->GetModel()->GetAnimClips();

            if (!clips.empty())
            {
                InstanceSettings settings = instance->GetInstanceSettings();
                if (settings.mAnimClipNr >= clips.size())
                {
                    settings.mAnimClipNr = 0;
                    settings.mAnimPlayTimePos = 0.0f;
                }
                
                std::string selectedClipNameStr = clips[settings.mAnimClipNr]->GetClipName();

                ImGui::BeginDisabled(!editMode);

                const unsigned int clipBefore = settings.mAnimClipNr;
                if (ImGui::BeginCombo("Clip", selectedClipNameStr.data()))
                {
                    for (int i = 0; i < static_cast<int>(clips.size()); ++i)
                    {
                        const bool isSelected = settings.mAnimClipNr == static_cast<unsigned int>(i);
                        if (ImGui::Selectable(clips[i]->GetClipName().c_str(), isSelected))
                        {
                            settings.mAnimClipNr = i;
                        }

                        if (isSelected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                if (settings.mAnimClipNr != clipBefore)
                {
                    // Discrete combo selection: record immediately (before = previous clip index).
                    InstanceSettings before = settings;
                    before.mAnimClipNr = clipBefore;
                    sceneEditor.RecordInstanceEdit(instance, before, settings, "Change Clip");
                }

                ImGui::SliderFloat("Speed", &settings.mAnimSpeedFactor, 0.0f, 2.0f, "%.2f", sliderFlags);
                trackSettingsEdit(sceneEditor, instance, settings, "Change Speed");
                instance->SetInstanceSettings(settings);

                ImGui::EndDisabled();
            }
            else
            {
                ImGui::TextUnformatted("Selected instance has no animation clips.");
            }
        }
    }

    ImGui::End();

    renderData.rdUIGenerateTime = mUIGenerateTimer.Stop();
}

void UserInterface::Render(RRenderData& renderData)
{
    mUIDrawTimer.Start();
    ImGui::Render();
    renderData.rdUIDrawTime = mUIDrawTimer.Stop();
}

void UserInterface::Cleanup(RRenderData& renderData)
{
    if (renderData.rdImgui != nullptr)
    {
        renderData.NRI.DestroyImgui(renderData.rdImgui);
        renderData.rdImgui = nullptr;
    }

    ImGui_ImplSDL3_Shutdown();

    if (ImGui::GetCurrentContext() != nullptr)
    {
        ImGui::DestroyContext();
    }
}
