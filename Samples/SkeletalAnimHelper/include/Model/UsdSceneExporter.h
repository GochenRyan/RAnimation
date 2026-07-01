#pragma once
#include <string>
#include <vector>

#include <Model/ModelAndInstanceData.h>
#include <Model/InstanceSettings.h>

// USD-free interface for exporting/importing the current scene to a USD stage. All pxr/USD headers stay
// inside UsdSceneExporter.cpp (pimpl-style), mirroring UsdModelLoader.

namespace RAnimation
{
    // Writes a USD composition to outPath: one prim per ModelInstance, each referencing its source asset
    // USD, with translate/rotate/scale xformOps and the selected clip / swap-axis recorded as customData.
    // Returns false on failure (message printed). Pure CPU work; no NRI/Renderer involvement.
    bool ExportSceneToUsd(const ModelAndInstanceData& modInstData, const std::string& outPath);

    // One instance parsed from a scene USD written by ExportSceneToUsd. assetPath is resolved absolute
    // (loadable by Model::LoadModel); settings carry the per-instance transform / clip / swap-axis.
    struct ImportedSceneInstance
    {
        std::string assetPath;
        InstanceSettings settings;
    };

    // Reads a scene USD (as written by ExportSceneToUsd) into a flat instance list. The caller loads each
    // asset and recreates the instances (the exporter/importer stay Renderer-agnostic). Returns false on
    // failure (message printed).
    bool ImportSceneFromUsd(const std::string& scenePath, std::vector<ImportedSceneInstance>& out);
} // namespace RAnimation
