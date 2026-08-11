/* separate settings file to avoid cicrula dependecies */
#pragma once

#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>

// forward declaration
namespace RAnimation
{
    class Model;
    class ModelInstance;
}

// Pure scene data. Owned by the SceneEditor; the Renderer reads it to draw, the UI reads/edits the
// selection. Structural mutation and undo/redo live in the SceneEditor, not here.
struct ModelAndInstanceData
{
    std::vector<std::shared_ptr<RAnimation::Model>> miModelList{};
    int miSelectedModel = 0;

    std::vector<std::shared_ptr<RAnimation::ModelInstance>> miModelInstances{};
    std::unordered_map<std::string, std::vector<std::shared_ptr<RAnimation::ModelInstance>>> miModelInstancesPerModel{};
    int miSelectedInstance = 0;

    /* delete models that were loaded during application runtime */
    std::unordered_set<std::shared_ptr<RAnimation::Model>> miPendingDeleteModels{};
};
