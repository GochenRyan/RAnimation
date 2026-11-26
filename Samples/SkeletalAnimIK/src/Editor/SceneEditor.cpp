#include <algorithm>
#include <filesystem>
#include <limits>

#include <fmt/base.h>
#include <fmt/color.h>

#include <Editor/SceneEditor.h>
#include <Editor/Command.h>
#include <Editor/InstanceSettingsCommand.h>

using namespace RAnimation;

void SceneEditor::SetGpuHooks(LoadModelHook loadModel, ReleaseModelHook releaseModel, FocusCameraHook focusCamera)
{
    mLoadModel = std::move(loadModel);
    mReleaseModel = std::move(releaseModel);
    mFocusCamera = std::move(focusCamera);
}

void SceneEditor::RecordInstanceEdit(const std::shared_ptr<ModelInstance>& instance,
                                     const InstanceSettings& before,
                                     const InstanceSettings& after,
                                     const char* name)
{
    if (instance == nullptr)
    {
        return;
    }

    mEditor.Record(std::make_unique<InstanceSettingsCommand>(name, instance, before, after));
}

bool SceneEditor::HasModel(const std::string& modelFileName) const
{
    auto modelIter = std::find_if(mModelInstData.miModelList.begin(),
                                  mModelInstData.miModelList.end(),
                                  [&modelFileName](const auto& model)
                                  {
                                      return model->GetModelFileNamePath() == modelFileName ||
                                             model->GetModelFileName() == modelFileName;
                                  });
    return modelIter != mModelInstData.miModelList.end();
}

std::shared_ptr<Model> SceneEditor::GetModel(const std::string& modelFileName) const
{
    auto modelIter = std::find_if(mModelInstData.miModelList.begin(),
                                  mModelInstData.miModelList.end(),
                                  [&modelFileName](const auto& model)
                                  {
                                      return model->GetModelFileNamePath() == modelFileName ||
                                             model->GetModelFileName() == modelFileName;
                                  });
    if (modelIter != mModelInstData.miModelList.end())
    {
        return *modelIter;
    }
    return nullptr;
}

bool SceneEditor::AddModel(const std::string& modelFileName)
{
    if (HasModel(modelFileName))
    {
        fmt::print("{} warning: model '{}' already existed, skipping\n", __FUNCTION__, modelFileName);
        return true;
    }

    const bool shouldFocusImportedModel = mModelInstData.miModelList.empty();

    std::shared_ptr<Model> model = mLoadModel ? mLoadModel(modelFileName) : nullptr;
    if (model == nullptr)
    {
        return false;
    }

    /* also add a new instance here to see the model */
    std::shared_ptr<ModelInstance> instance = std::make_shared<ModelInstance>(model);

    // Import = one undo step that inserts/removes the model together with its first instance. The
    // GPU upload above runs once; redo only re-inserts the already-loaded pointers. When this command
    // is dropped from history while the model is out of the scene (!applied), free its GPU resources.
    mEditor.Execute(std::make_unique<FunctionalCommand>(
            "Import Model",
            [this, model, instance]()
            {
                insertModelInternal(model);
                insertInstanceInternal(instance);
            },
            [this, model, instance]()
            {
                removeInstanceInternal(instance);
                removeModelInternal(model);
            },
            [this, model](bool currentlyApplied)
            {
                if (!currentlyApplied)
                {
                    releaseModelGpu(model);
                }
            }));

    if (shouldFocusImportedModel && instance != nullptr)
    {
        // Reuse the Center-button path so an imported model frames on its joint AABB centre, not the
        // feet origin (which sat the model too high in view).
        FocusCameraOn(instance);
    }

    return true;
}

void SceneEditor::DeleteModel(const std::string& modelFileName)
{
    std::string shortModelFileName = std::filesystem::path(modelFileName).filename().generic_string();

    std::vector<std::shared_ptr<Model>> modelsToDelete;
    for (const auto& model : mModelInstData.miModelList)
    {
        if (model != nullptr &&
            (model->GetModelFileName() == shortModelFileName || model->GetModelFileNamePath() == modelFileName))
        {
            modelsToDelete.emplace_back(model);
        }
    }

    if (modelsToDelete.empty())
    {
        return;
    }

    // Snapshot every instance belonging to the deleted models so the command can restore them on undo.
    std::vector<std::shared_ptr<ModelInstance>> instancesToDelete;
    for (const auto& instance : mModelInstData.miModelInstances)
    {
        if (instance->GetModel()->GetModelFileName() == shortModelFileName)
        {
            instancesToDelete.emplace_back(instance);
        }
    }

    // Delete = one undo step. Do() removes instances + models from the live lists but keeps the
    // shared_ptrs alive in the captures (no GPU free). On discard while applied (model still removed),
    // release the GPU resources.
    mEditor.Execute(std::make_unique<FunctionalCommand>(
            "Delete Model",
            [this, modelsToDelete, instancesToDelete]()
            {
                for (const auto& instance : instancesToDelete)
                {
                    removeInstanceInternal(instance);
                }
                for (const auto& model : modelsToDelete)
                {
                    removeModelInternal(model);
                }
            },
            [this, modelsToDelete, instancesToDelete]()
            {
                for (const auto& model : modelsToDelete)
                {
                    insertModelInternal(model);
                }
                for (const auto& instance : instancesToDelete)
                {
                    insertInstanceInternal(instance);
                }
            },
            [this, modelsToDelete](bool currentlyApplied)
            {
                if (currentlyApplied)
                {
                    for (const auto& model : modelsToDelete)
                    {
                        releaseModelGpu(model);
                    }
                }
            }));
}

std::shared_ptr<ModelInstance> SceneEditor::AddInstance(std::shared_ptr<Model> model)
{
    std::shared_ptr<ModelInstance> newInst = std::make_shared<ModelInstance>(model);

    mEditor.Execute(std::make_unique<FunctionalCommand>(
            "Add Instance",
            [this, newInst]() { insertInstanceInternal(newInst); },
            [this, newInst]() { removeInstanceInternal(newInst); }));

    return newInst;
}

void SceneEditor::AddInstances(std::shared_ptr<Model> model, int numInstances)
{
    size_t animClipNum = model->GetAnimClips().size();

    std::vector<std::shared_ptr<ModelInstance>> createdInstances;
    createdInstances.reserve(static_cast<size_t>(std::max(0, numInstances)));
    for (int i = 0; i < numInstances; ++i)
    {
        int xPos = std::rand() % 50 - 25;
        int zPos = std::rand() % 50 - 25;
        int rotation = std::rand() % 360 - 180;
        int clipNr = animClipNum > 0 ? std::rand() % animClipNum : 0;

        std::shared_ptr<ModelInstance> newInstance =
                std::make_shared<ModelInstance>(model, glm::vec3(xPos, 0.0f, zPos), glm::vec3(0.0f, rotation, 0.0f));
        InstanceSettings instSettings = newInstance->GetInstanceSettings();
        if (animClipNum > 0)
        {
            instSettings.mAnimClipNr = clipNr;
        }
        instSettings.mAnimPhase = static_cast<float>(std::rand() % 1000) / 1000.0f;
        newInstance->SetInstanceSettings(instSettings);

        createdInstances.emplace_back(std::move(newInstance));
    }

    if (createdInstances.empty())
    {
        return;
    }

    mEditor.Execute(std::make_unique<FunctionalCommand>(
            "Create Instances",
            [this, createdInstances]()
            {
                for (const auto& instance : createdInstances)
                {
                    insertInstanceInternal(instance);
                }
            },
            [this, createdInstances]()
            {
                for (const auto& instance : createdInstances)
                {
                    removeInstanceInternal(instance);
                }
            }));
}

void SceneEditor::AddInstances(std::shared_ptr<Model> model, const std::vector<InstanceSettings>& settings, const char* commandName)
{
    std::vector<std::shared_ptr<ModelInstance>> createdInstances;
    createdInstances.reserve(settings.size());
    for (const InstanceSettings& s : settings)
    {
        std::shared_ptr<ModelInstance> newInstance = std::make_shared<ModelInstance>(model, s.mWorldPosition, s.mWorldRotation, s.mScale);
        newInstance->SetInstanceSettings(s);
        createdInstances.emplace_back(std::move(newInstance));
    }
    if (createdInstances.empty())
    {
        return;
    }

    mEditor.Execute(std::make_unique<FunctionalCommand>(
            commandName,
            [this, createdInstances]()
            {
                for (const auto& instance : createdInstances)
                {
                    insertInstanceInternal(instance);
                }
            },
            [this, createdInstances]()
            {
                for (const auto& instance : createdInstances)
                {
                    removeInstanceInternal(instance);
                }
            }));
}

void SceneEditor::DeleteInstance(std::shared_ptr<ModelInstance> instance)
{
    mEditor.Execute(std::make_unique<FunctionalCommand>(
            "Delete Instance",
            [this, instance]() { removeInstanceInternal(instance); },
            [this, instance]() { insertInstanceInternal(instance); }));
}

std::shared_ptr<ModelInstance> SceneEditor::CloneInstance(std::shared_ptr<ModelInstance> instance)
{
    std::shared_ptr<Model> currentModel = instance->GetModel();
    std::shared_ptr<ModelInstance> newInstance = std::make_shared<ModelInstance>(currentModel);
    InstanceSettings newInstanceSettings = instance->GetInstanceSettings();

    /* slight offset to see new instance */
    newInstanceSettings.mWorldPosition += glm::vec3(1.0f, 0.0f, -1.0f);
    newInstance->SetInstanceSettings(newInstanceSettings);

    // same behaviour setup, but only one agent may start possessed; the entity id is assigned on insert
    std::optional<AgentConfig> agent = instance->GetAgentConfig();
    if (agent)
    {
        agent->possessed = false;
    }
    newInstance->SetAgentConfig(agent);

    mEditor.Execute(std::make_unique<FunctionalCommand>(
            "Clone Instance",
            [this, newInstance]() { insertInstanceInternal(newInstance); },
            [this, newInstance]() { removeInstanceInternal(newInstance); }));
    return newInstance;
}

SceneEditor::ImportResult SceneEditor::ImportScene(const ImportedScene& scene)
{
    ImportResult result;
    std::vector<std::shared_ptr<Model>> newModels;
    std::vector<std::shared_ptr<ModelInstance>> newInstances;
    std::vector<StaticLayer> newLayers;

    // GPU loads happen once, here; the command only (re)inserts the loaded pointers.
    const auto findOrLoad = [&](const std::string& path) -> std::shared_ptr<Model>
    {
        if (std::shared_ptr<Model> model = GetModel(path))
        {
            return model;
        }
        for (const std::shared_ptr<Model>& model : newModels)
        {
            if (model->GetModelFileNamePath() == path)
            {
                return model;
            }
        }
        std::shared_ptr<Model> model = mLoadModel ? mLoadModel(path) : nullptr;
        if (model != nullptr)
        {
            newModels.push_back(model);
        }
        return model;
    };

    bool possessedSeen = false;
    for (const ImportedSceneInstance& imp : scene.instances)
    {
        std::shared_ptr<Model> model = findOrLoad(imp.assetPath);
        if (model == nullptr)
        {
            ++result.failedAssets;
            continue;
        }
        auto instance = std::make_shared<ModelInstance>(model, imp.settings.mWorldPosition, imp.settings.mWorldRotation, imp.settings.mScale);
        instance->SetInstanceSettings(imp.settings);

        const EntityId id = claimEntityId(imp.entityId);
        if (imp.entityId == kInvalidEntityId)
        {
            ++result.newIds;
        }
        else if (id != imp.entityId)
        {
            ++result.reassignedIds;
            fmt::print(stderr, fg(fmt::color::yellow), "SceneEditor: '{}' entity id {} is already in use, assigned {}\n", imp.primPath, imp.entityId, id);
        }
        instance->SetEntityId(id);

        std::optional<AgentConfig> agent = imp.agent;
        if (agent && agent->possessed)
        {
            if (possessedSeen)
            {
                fmt::print(stderr, fg(fmt::color::yellow), "SceneEditor: '{}' is a second possessed agent, cleared\n", imp.primPath);
                agent->possessed = false;
            }
            possessedSeen = true;
        }
        instance->SetAgentConfig(std::move(agent));
        newInstances.push_back(std::move(instance));
    }

    for (const ImportedStaticLayer& imported : scene.staticLayers)
    {
        const bool alreadyLoaded = std::any_of(mModelInstData.miStaticLayers.begin(),
                                               mModelInstData.miStaticLayers.end(),
                                               [&imported](const StaticLayer& layer) { return layer.path == imported.path; });
        if (alreadyLoaded)
        {
            fmt::print(stderr, fg(fmt::color::yellow), "SceneEditor: static layer '{}' is already in the scene, skipped\n", imported.path);
            continue;
        }
        StaticLayer layer;
        layer.path = imported.path;
        layer.statics = imported.statics;
        // a layer with only markers has no display model; its gameplay data still counts
        layer.model = findOrLoad(imported.path);
        if (layer.model != nullptr)
        {
            newInstances.push_back(std::make_shared<ModelInstance>(layer.model));
        }
        newLayers.push_back(std::move(layer));
    }

    result.instances = static_cast<int>(scene.instances.size()) - result.failedAssets;
    result.staticLayers = static_cast<int>(newLayers.size());
    if (newInstances.empty() && newLayers.empty())
    {
        return result;
    }

    mEditor.Execute(std::make_unique<FunctionalCommand>(
            "Import Scene",
            [this, newModels, newInstances, newLayers]()
            {
                for (const auto& model : newModels)
                {
                    insertModelInternal(model);
                }
                for (const auto& instance : newInstances)
                {
                    insertInstanceInternal(instance);
                }
                for (const StaticLayer& layer : newLayers)
                {
                    mModelInstData.miStaticLayers.push_back(layer);
                }
            },
            [this, newModels, newInstances, newLayers]()
            {
                auto& layers = mModelInstData.miStaticLayers;
                for (const StaticLayer& layer : newLayers)
                {
                    layers.erase(std::remove_if(layers.begin(), layers.end(), [&layer](const StaticLayer& l) { return l.path == layer.path; }),
                                 layers.end());
                }
                for (const auto& instance : newInstances)
                {
                    removeInstanceInternal(instance);
                }
                for (const auto& model : newModels)
                {
                    removeModelInternal(model);
                }
            },
            [this, newModels](bool currentlyApplied)
            {
                if (!currentlyApplied)
                {
                    for (const auto& model : newModels)
                    {
                        releaseModelGpu(model);
                    }
                }
            }));
    return result;
}

void SceneEditor::ClearForLoad()
{
    mEditor.Clear();
    mModelInstData.miModelInstances.clear();
    mModelInstData.miModelInstancesPerModel.clear();
    mModelInstData.miStaticLayers.clear();
    mModelInstData.miSelectedInstance = 0;
    mUsedEntityIds.clear();
    mNextEntityId = 1;
}

EntityId SceneEditor::AllocateEntityId()
{
    while (mUsedEntityIds.count(mNextEntityId) != 0)
    {
        ++mNextEntityId;
    }
    const EntityId id = mNextEntityId++;
    mUsedEntityIds.insert(id);
    return id;
}

EntityId SceneEditor::claimEntityId(EntityId wanted)
{
    if (wanted != kInvalidEntityId && mUsedEntityIds.insert(wanted).second)
    {
        mNextEntityId = std::max(mNextEntityId, wanted + 1);
        return wanted;
    }
    return AllocateEntityId();
}

std::shared_ptr<ModelInstance> SceneEditor::FindEntity(EntityId id) const
{
    for (const std::shared_ptr<ModelInstance>& instance : mModelInstData.miModelInstances)
    {
        if (instance != nullptr && instance->GetEntityId() == id)
        {
            return instance;
        }
    }
    return nullptr;
}

void SceneEditor::FocusCameraOn(std::shared_ptr<ModelInstance> instance)
{
    if (instance == nullptr || !mFocusCamera)
    {
        return;
    }

    // The instance origin sits at the feet, so focusing on it frames the model too high. Aim at the
    // centre of the joint AABB instead (roughly mid-body). Refresh the pose first (advance by 0 so the
    // play time does not step) so the joint world matrices reflect this instance.
    glm::vec3 focus = instance->GetWorldPosition();
    std::vector<glm::mat4> globals;
    instance->GetModel()->ComputeNodeGlobals(instance->GetCpuPose(), instance->GetLocalTransformMatrix(), globals);
    if (!globals.empty())
    {
        glm::vec3 mn(std::numeric_limits<float>::max());
        glm::vec3 mx(std::numeric_limits<float>::lowest());
        for (const glm::mat4& m : globals)
        {
            const glm::vec3 p = glm::vec3(m[3]);
            mn = glm::min(mn, p);
            mx = glm::max(mx, p);
        }
        focus = (mn + mx) * 0.5f;
    }

    mFocusCamera(focus);
}

void SceneEditor::ReleaseAllGpuResources()
{
    // History-held removed models first (their commands free them in the correct applied-state),
    // then every model still live in the scene.
    mEditor.Clear();

    for (const auto& model : mModelInstData.miModelList)
    {
        releaseModelGpu(model);
    }
}

void SceneEditor::insertModelInternal(const std::shared_ptr<Model>& model)
{
    if (model == nullptr)
    {
        return;
    }

    mModelInstData.miModelList.emplace_back(model);
}

void SceneEditor::removeModelInternal(const std::shared_ptr<Model>& model)
{
    auto& modelList = mModelInstData.miModelList;
    modelList.erase(std::remove(modelList.begin(), modelList.end(), model), modelList.end());
}

void SceneEditor::insertInstanceInternal(const std::shared_ptr<ModelInstance>& instance)
{
    if (instance == nullptr)
    {
        return;
    }
    // first insertion assigns the id; redo / undo-of-delete re-insert the same object and keep it
    if (instance->GetEntityId() == kInvalidEntityId)
    {
        instance->SetEntityId(AllocateEntityId());
    }

    mModelInstData.miModelInstances.emplace_back(instance);
    mModelInstData.miModelInstancesPerModel[instance->GetModel()->GetModelFileName()].emplace_back(instance);
}

void SceneEditor::removeInstanceInternal(const std::shared_ptr<ModelInstance>& instance)
{
    if (instance == nullptr)
    {
        return;
    }

    auto& instances = mModelInstData.miModelInstances;
    instances.erase(std::remove(instances.begin(), instances.end(), instance), instances.end());

    const std::string modelName = instance->GetModel()->GetModelFileName();
    auto& perModel = mModelInstData.miModelInstancesPerModel[modelName];
    perModel.erase(std::remove(perModel.begin(), perModel.end(), instance), perModel.end());
}

void SceneEditor::releaseModelGpu(const std::shared_ptr<Model>& model)
{
    if (model == nullptr)
    {
        return;
    }

    // Model::Cleanup is not idempotent; ensure each model's GPU resources are released only once.
    if (!mReleasedModels.insert(model.get()).second)
    {
        return;
    }

    if (mReleaseModel)
    {
        mReleaseModel(model);
    }
    mModelInstData.miPendingDeleteModels.erase(model);
}
