#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include <Model/EntityId.h>
#include <Model/Model.h>
#include <Model/ModelInstance.h>
#include <Model/InstanceSettings.h>
#include <Model/ModelAndInstanceData.h>
#include <Model/UsdSceneExporter.h>
#include <Editor/Editor.h>

namespace RAnimation
{
    // Editor control layer. Owns the scene data (models/instances) and the undo/redo history + mode.
    // It performs every structural mutation and produces the commands that make them reversible.
    //
    // All GPU work (loading a model, freeing a model's GPU resources, moving the camera) is delegated
    // to injected hooks, so SceneEditor depends only on Model/glm and never on the Renderer or NRI.
    class SceneEditor final
    {
    public:
        using LoadModelHook = std::function<std::shared_ptr<Model>(const std::string&)>;
        using ReleaseModelHook = std::function<void(const std::shared_ptr<Model>&)>;
        using FocusCameraHook = std::function<void(const glm::vec3&)>;

        void SetGpuHooks(LoadModelHook loadModel, ReleaseModelHook releaseModel, FocusCameraHook focusCamera);

        ModelAndInstanceData& ModelData()
        {
            return mModelInstData;
        }

        // -- Mode --
        Editor::Mode GetMode() const
        {
            return mEditor.GetMode();
        }
        bool IsEditMode() const
        {
            return mEditor.IsEditMode();
        }
        void SetMode(Editor::Mode mode)
        {
            mEditor.SetMode(mode);
        }
        void ToggleMode()
        {
            mEditor.ToggleMode();
        }

        // -- Undo/redo (forwarded to the owned Editor) --
        bool CanUndo() const
        {
            return mEditor.CanUndo();
        }
        bool CanRedo() const
        {
            return mEditor.CanRedo();
        }
        const char* UndoName() const
        {
            return mEditor.UndoName();
        }
        const char* RedoName() const
        {
            return mEditor.RedoName();
        }
        void Undo()
        {
            mEditor.Undo();
        }
        void Redo()
        {
            mEditor.Redo();
        }

        // Records an instance-settings edit (transform / swap-axis / clip / speed) as one undo step.
        // The value is already applied live; this only pushes the before->after command.
        void RecordInstanceEdit(const std::shared_ptr<ModelInstance>& instance,
                                const InstanceSettings& before,
                                const InstanceSettings& after,
                                const char* name);

        // Generic command access for other editors (AnimGraph panel): Execute runs Do() then records;
        // Record only pushes an already-applied change (slider drags).
        void ExecuteCommand(std::unique_ptr<ICommand> command)
        {
            mEditor.Execute(std::move(command));
        }
        void RecordCommand(std::unique_ptr<ICommand> command)
        {
            mEditor.Record(std::move(command));
        }

        // -- Structural operations (each produces a command) --
        bool HasModel(const std::string& modelFileName) const;
        std::shared_ptr<Model> GetModel(const std::string& modelFileName) const;

        bool AddModel(const std::string& modelFileName);
        void DeleteModel(const std::string& modelFileName);

        std::shared_ptr<ModelInstance> AddInstance(std::shared_ptr<Model> model);
        void AddInstances(std::shared_ptr<Model> model, int numInstances);
        // Creates one instance per settings entry as a single undo step (crowd spawn).
        void AddInstances(std::shared_ptr<Model> model, const std::vector<InstanceSettings>& settings, const char* commandName);
        void DeleteInstance(std::shared_ptr<ModelInstance> instance);
        // The clone gets a new entity id; an agent config is copied with possessed cleared.
        std::shared_ptr<ModelInstance> CloneInstance(std::shared_ptr<ModelInstance> instance);
        void FocusCameraOn(std::shared_ptr<ModelInstance> instance);

        // -- Level import (one undo step) --
        // Loads every asset not yet in the scene, recreates the instances with their file entity ids and
        // agent configs, and adds the static layers (gameplay data + a display instance at the origin when
        // the layer has visible meshes). A file id already taken in this session is replaced by a fresh one.
        struct ImportResult
        {
            int instances = 0;
            int staticLayers = 0;
            int failedAssets = 0;
            int reassignedIds = 0; // file id already in use -> fresh id
            int newIds = 0;        // file had no id (pre-entity-id level) -> fresh id
        };
        ImportResult ImportScene(const ImportedScene& scene);

        // Checkpoint load: drops every instance and static layer and the undo history (not undoable); loaded models
        // stay in the model list so the following ImportScene reuses them. Entity ids become free again.
        void ClearForLoad();

        // -- Entity ids --
        // Every instance gets one when it enters the scene (insert); ids are never reused within a session,
        // so an instance restored by undo keeps a unique id.
        EntityId AllocateEntityId();
        std::shared_ptr<ModelInstance> FindEntity(EntityId id) const;

        // Frees the GPU resources of every model still referenced by the scene or the history. Must be
        // called at shutdown while the device is alive (drives the release hook).
        void ReleaseAllGpuResources();

    private:
        // CPU-side primitives the commands call (no GPU work, no disk load).
        void insertModelInternal(const std::shared_ptr<Model>& model);
        void removeModelInternal(const std::shared_ptr<Model>& model);
        void insertInstanceInternal(const std::shared_ptr<ModelInstance>& instance);
        void removeInstanceInternal(const std::shared_ptr<ModelInstance>& instance);

        // Frees a model's GPU resources exactly once via the release hook.
        void releaseModelGpu(const std::shared_ptr<Model>& model);

        // Reserves wanted when it is free, else allocates a fresh id (kInvalidEntityId -> fresh id).
        EntityId claimEntityId(EntityId wanted);

        EntityId mNextEntityId = 1;
        std::unordered_set<EntityId> mUsedEntityIds{};

        ModelAndInstanceData mModelInstData{};
        Editor mEditor{};

        LoadModelHook mLoadModel;
        ReleaseModelHook mReleaseModel;
        FocusCameraHook mFocusCamera;

        // Guards releaseModelGpu so a model's non-idempotent Model::Cleanup never runs twice.
        std::unordered_set<Model*> mReleasedModels{};
    };
} // namespace RAnimation
