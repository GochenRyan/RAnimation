/* separate settings file to avoid cicrula dependecies */
#pragma once

#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>

// forward declaration
class Model;
class ModelInstance;

using modelCheckCallback = std::function<bool(std::string)>;
using modelAddCallback = std::function<bool(std::string)>;
using modelDeleteCallback = std::function<void(std::string)>;

using instanceAddCallback = std::function<std::shared_ptr<ModelInstance>(std::shared_ptr<Model>)>;
using instanceAddManyCallback = std::function<void(std::shared_ptr<Model>, int)>;
using instanceDeleteCallback = std::function<void(std::shared_ptr<ModelInstance>)>;
using instanceCloneCallback = std::function<void(std::shared_ptr<ModelInstance>)>;

struct ModelAndInstanceData
{
    std::vector<std::shared_ptr<Model>> miModelList{};
    int miSelectedModel = 0;

    std::vector<std::shared_ptr<ModelInstance>> miModelInstances{};
    std::unordered_map<std::string, std::vector<std::shared_ptr<ModelInstance>>> miModelInstancesPerModel{};
    int miSelectedInstance = 0;

    /* delete models that were loaded during application runtime */
    std::unordered_set<std::shared_ptr<Model>> miPendingDeleteModels{};

    /* callbacks */
    modelCheckCallback miModelCheckCallbackFunction;
    modelAddCallback miModelAddCallbackFunction;
    modelDeleteCallback miModelDeleteCallbackFunction;

    instanceAddCallback miInstanceAddCallbackFunction;
    instanceAddManyCallback miInstanceAddManyCallbackFunction;
    instanceDeleteCallback miInstanceDeleteCallbackFunction;
    instanceCloneCallback miInstanceCloneCallbackFunction;
};
