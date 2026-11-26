#pragma once
#include <array>
#include <optional>
#include <string>
#include <vector>

#include <Model/EntityId.h>
#include <Model/LevelStatics.h>
#include <Model/ModelAndInstanceData.h>
#include <Model/InstanceSettings.h>
#include <Tools/Camera.h>

// USD-free interface for exporting/importing the current scene to a USD stage. All pxr/USD headers stay
// inside UsdSceneExporter.cpp (pimpl-style), mirroring UsdModelLoader.
//
// Level layout (Assets/SkeletalAnimBehavior/level):
//   <name>.level.usda   written by ExportSceneToUsd: /Scene (camera rig) + one referencing prim per instance
//                       (entity id, agent config, clip as customData) + subLayers = the static layers
//   <name>.static.usda  written by the DCC (Tools/Maya/BehaviorScene): /Static with display meshes and
//                       ranim:* tagged colliders / zones / markers. The engine never writes it.

namespace RAnimation
{
    // Writes a USD composition to outPath: one prim per ModelInstance, each referencing its source asset
    // USD, with translate/rotate/scale xformOps and editor state as customData (ranim:entityId,
    // ranim:agent, ranim:animClipNr ...). The camera rig (all four cameras + active slot) is written as
    // customData on the /Scene root prim. Static layers (modInstData.miStaticLayers) are written back as
    // subLayers; the instances showing their display models are skipped. Returns false on failure (message
    // printed). Pure CPU work; no NRI/Renderer involvement.
    bool ExportSceneToUsd(const ModelAndInstanceData& modInstData, const CameraRig& cameraRig,
                          const std::string& outPath);

    // One instance parsed from a scene USD written by ExportSceneToUsd. assetPath is resolved absolute
    // (loadable by Model::LoadModel); settings carry the per-instance transform / clip.
    struct ImportedSceneInstance
    {
        std::string assetPath;
        std::string primPath;
        InstanceSettings settings;
        EntityId entityId = kInvalidEntityId; // kInvalidEntityId: the file predates entity ids
        std::optional<AgentConfig> agent;
    };

    struct ImportedStaticLayer
    {
        std::string path; // absolute
        LevelStatics statics;
    };

    struct ImportedScene
    {
        std::vector<ImportedSceneInstance> instances;
        std::vector<ImportedStaticLayer> staticLayers; // the level's subLayers, in authored order
        CameraRig camera;                              // slots without authored keys keep their defaults
    };

    // Reads a scene USD (as written by ExportSceneToUsd) into a flat instance list plus its static layers.
    // The caller loads each asset and recreates the instances (SceneEditor::ImportScene; the exporter /
    // importer stay Renderer-agnostic). Returns false on failure (message printed).
    bool ImportSceneFromUsd(const std::string& scenePath, ImportedScene& out);

    // Reads the gameplay content (colliders, zones, markers) of one DCC-authored static layer. Transforms
    // are the full stage-to-canonical * local-to-world. Naming / attribute mismatches end up in
    // out.warnings (and are printed); only an unreadable file returns false.
    bool LoadLevelStatics(const std::string& staticLayerPath, LevelStatics& out, std::string& error);
} // namespace RAnimation
