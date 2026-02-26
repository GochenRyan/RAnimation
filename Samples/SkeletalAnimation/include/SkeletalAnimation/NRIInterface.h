#pragma once
// NRI: core & extensions
#include "NRI.h"

#include "Extensions/NRIDeviceCreation.h"
#include "Extensions/NRIHelper.h"
#include "Extensions/NRIImgui.h"
#include "Extensions/NRILowLatency.h"
#include "Extensions/NRIMeshShader.h"
#include "Extensions/NRIRayTracing.h"
#include "Extensions/NRIStreamer.h"
#include "Extensions/NRISwapChain.h"
#include "Extensions/NRIUpscaler.h"

struct NRIInterface
    : public nri::CoreInterface,
      public nri::HelperInterface,
      public nri::LowLatencyInterface,
      public nri::MeshShaderInterface,
      public nri::RayTracingInterface,
      public nri::StreamerInterface,
      public nri::SwapChainInterface,
      public nri::UpscalerInterface {
    inline bool HasCore() const {
        return GetDeviceDesc != nullptr;
    }

    inline bool HasHelper() const {
        return CalculateAllocationNumber != nullptr;
    }

    inline bool HasLowLatency() const {
        return SetLatencySleepMode != nullptr;
    }

    inline bool HasMeshShader() const {
        return CmdDrawMeshTasks != nullptr;
    }

    inline bool HasRayTracing() const {
        return CreateRayTracingPipeline != nullptr;
    }

    inline bool HasStreamer() const {
        return CreateStreamer != nullptr;
    }

    inline bool HasSwapChain() const {
        return CreateSwapChain != nullptr;
    }

    inline bool HasUpscaler() const {
        return CreateUpscaler != nullptr;
    }
};

struct SwapChainTexture {
    nri::Fence* acquireSemaphore;
    nri::Fence* releaseSemaphore;
    nri::Texture* texture;
    nri::Descriptor* colorAttachment;
    nri::Format attachmentFormat;
};