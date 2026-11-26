/* separate settings file to avoid cicrula dependecies */
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include <AnimGraph/EvalMode.h>
#include <Model/LevelStatics.h>
#include <Tools/DebugLines.h>

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

    /* per-frame CPU animation statistics (AnimationSystem::Update) */
    struct AnimStats
    {
        int instancesTicked = 0;
        int instancesTracked = 0;
        int instancesFailed = 0;
        int updateVisits = 0;
        int evaluateVisits = 0;
        size_t slotTransforms = 0;
        size_t terms = 0;
        float cpuMs = 0.0f;
    } miAnimStats{};

    /* GPU animation evaluation settings (edited by the UI, read by AnimationSystem and the renderer) */
    RAnimation::EvalMode miEvalMode = RAnimation::EvalMode::Cpu;
    int miTrackPhases = 8;            // tier C: phase buckets per clip (tracks = clips x phases)
    bool miTrsInterpolate = true;     // tier C: interpolate between texture rows (off = nearest row)
    float miTrackGlobalTimeSec = 0.0f; // advanced once per frame by AnimationSystem

    /* debug lines for this frame (CollisionSystem::BuildDebugLines + level statics), drawn by GizmoDrawPass */
    RAnimation::DebugLineList miDebugLines{};

    /* DCC-authored static sublayers of the imported level (colliders, zones, markers, display model). The
       scene exporter writes them back as subLayers and skips the instances showing their display models. */
    std::vector<RAnimation::StaticLayer> miStaticLayers{};
    bool miShowLevelStatics = true;

    bool IsStaticLayerModel(const RAnimation::Model* model) const
    {
        for (const RAnimation::StaticLayer& layer : miStaticLayers)
        {
            if (model != nullptr && layer.model.get() == model)
            {
                return true;
            }
        }
        return false;
    }

    /* delete models that were loaded during application runtime */
    std::unordered_set<std::shared_ptr<RAnimation::Model>> miPendingDeleteModels{};
};
