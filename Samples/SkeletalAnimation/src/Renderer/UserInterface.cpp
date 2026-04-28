#include <algorithm>
#include <filesystem>
#include <limits>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <ImGuiFileDialog.h>

#include <Model\InstanceSettings.h>
#include <Model\Model.h>
#include <Model\ModelInstance.h>
#include <Renderer/UserInterface.h>

using namespace RAnimation;

namespace
{
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

void UserInterface::CreateFrame(RRenderData& renderData, ModelAndInstanceData& modInstData)
{
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

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

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Camera Position: %s", glm::to_string(renderData.rdCameraWorldPosition).c_str());
        ImGui::SliderInt("Field of View", &renderData.rdFieldOfView, 40, 150, "%d", sliderFlags);
        ImGui::SliderFloat("Azimuth", &renderData.rdViewAzimuth, 0.0f, 360.0f, "%.1f", sliderFlags);
        ImGui::SliderFloat("Elevation", &renderData.rdViewElevation, -89.0f, 89.0f, "%.1f", sliderFlags);
        ImGui::SliderFloat3(
                "Position", glm::value_ptr(renderData.rdCameraWorldPosition), -50.0f, 50.0f, "%.2f", sliderFlags);
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
        if (ImGui::Button("Import Model"))
        {
            IGFD::FileDialogConfig config;
            config.path = ".";
            config.countSelectionMax = 1;
            config.flags = ImGuiFileDialogFlags_Modal;
            ImGuiFileDialog::Instance()->OpenDialog("ChooseModelFile",
                                                    "Choose Model File",
                                                    "Supported Model Files{.gltf,.glb,.obj,.fbx,.dae,.mdl,.md3,.pk3}",
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

                if (modInstData.miModelAddCallbackFunction)
                {
                    modInstData.miModelAddCallbackFunction(filePathName);
                    modInstData.miSelectedModel = std::max(0, static_cast<int>(modInstData.miModelList.size()) - 1);
                    modInstData.miSelectedInstance =
                            std::max(0, static_cast<int>(modInstData.miModelInstances.size()) - 1);
                }
            }

            ImGuiFileDialog::Instance()->Close();
        }

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

            if (ImGui::Button("Delete Model") && modInstData.miModelDeleteCallbackFunction)
            {
                modInstData.miModelDeleteCallbackFunction(
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
                    ImGui::End();
                    return;
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Create Instance") && modInstData.miInstanceAddCallbackFunction)
            {
                modInstData.miInstanceAddCallbackFunction(modInstData.miModelList[modInstData.miSelectedModel]);
                modInstData.miSelectedInstance = std::max(0, static_cast<int>(modInstData.miModelInstances.size()) - 1);
            }

            ImGui::SameLine();
            if (ImGui::Button("Create Many") && modInstData.miInstanceAddManyCallbackFunction)
            {
                modInstData.miInstanceAddManyCallbackFunction(modInstData.miModelList[modInstData.miSelectedModel],
                                                              mManyInstanceCreateNum);
                modInstData.miSelectedInstance = std::max(0, static_cast<int>(modInstData.miModelInstances.size()) - 1);
            }

            ImGui::SameLine();
            ImGui::SliderInt("##ManyCount", &mManyInstanceCreateNum, 1, 100, "%d", sliderFlags);
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

            if (ImGui::Button("Clone Instance") && modInstData.miInstanceCloneCallbackFunction)
            {
                modInstData.miInstanceCloneCallbackFunction(instance);
                modInstData.miSelectedInstance = std::max(0, static_cast<int>(modInstData.miModelInstances.size()) - 1);
            }

            ImGui::SameLine();
            const std::string modelName = instance->GetModel()->GetModelFileName();
            const size_t perModelCount = modInstData.miModelInstancesPerModel[modelName].size();
            if (perModelCount < 2)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Delete Instance") && modInstData.miInstanceDeleteCallbackFunction)
            {
                modInstData.miInstanceDeleteCallbackFunction(instance);
                modInstData.miSelectedInstance =
                        std::clamp(modInstData.miSelectedInstance - 1,
                                   0,
                                   std::max(0, static_cast<int>(modInstData.miModelInstances.size()) - 1));

                if (modInstData.miModelInstances.empty())
                {
                    ImGui::End();
                    return;
                }
            }
            if (perModelCount < 2)
            {
                ImGui::EndDisabled();
            }

            ImGui::Checkbox("Swap Y/Z", &settings.mSwapYZAxis);
            ImGui::SliderFloat3(
                    "World Position", glm::value_ptr(settings.mWorldPosition), -25.0f, 25.0f, "%.2f", sliderFlags);
            ImGui::SliderFloat3(
                    "World Rotation", glm::value_ptr(settings.mWorldRotation), -180.0f, 180.0f, "%.1f", sliderFlags);
            ImGui::SliderFloat("Scale", &settings.mScale, 0.01f, 10.0f, "%.3f", sliderFlags);

            instance->SetInstanceSettings(settings);
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
                const char* selectedClipName = clips[settings.mAnimClipNr]->GetClipName().c_str();

                if (ImGui::BeginCombo("Clip", selectedClipName))
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

                ImGui::SliderFloat("Speed", &settings.mAnimSpeedFactor, 0.0f, 2.0f, "%.2f", sliderFlags);
                instance->SetInstanceSettings(settings);
            }
            else
            {
                ImGui::TextUnformatted("Selected instance has no animation clips.");
            }
        }
    }

    ImGui::End();
}

void UserInterface::Render(RRenderData& renderData)
{
    ImGui::Render();
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
