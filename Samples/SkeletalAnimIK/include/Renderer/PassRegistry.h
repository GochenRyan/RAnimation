#pragma once

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Renderer/IRenderPass.h>

namespace RAnimation
{
    // Owns all IRenderPass instances. Drives their lifecycle, per-phase Record(),
    // and auto-generates buffer barriers from each pass's DeclareAccess() declarations.
    class PassRegistry
    {
    public:
        template <typename PassT, typename... Args>
        PassT* Add(Args&&... args)
        {
            auto pass = std::make_unique<PassT>(std::forward<Args>(args)...);
            PassT* raw = pass.get();
            mPasses.push_back(std::move(pass));
            return raw;
        }

        bool DeclareResources(ResourceContext& context);
        bool CreatePipelines(RenderContext& context);
        DescriptorPoolRequirements GetDescriptorPoolRequirements(uint32_t queuedFrameNum) const;
        bool CreateDescriptors(FrameContext& context);

        // Per-frame host-side data marshaling. Calls each pass's Upload() in registration order
        // before any RecordPhase. context.sceneFrame must be non-null.
        void UploadFrame(FrameContext& context);

        // Run all passes whose GetPhase() matches `phase`, in registration order.
        // Before each pass's Record(), emits any barriers required to transition
        // declared buffers from their last-known access state to the pass's declared state.
        void RecordPhase(CommandContext& context, RenderPassPhase phase);

        void Cleanup(RRenderData& renderData);

    private:
        struct LastAccessState
        {
            nri::AccessBits access = nri::AccessBits::NONE;
            nri::StageBits stage = nri::StageBits::NONE;
            bool initialized = false;
        };

        void EmitBarriersForPass(CommandContext& context, IRenderPass& pass);

        std::vector<std::unique_ptr<IRenderPass>> mPasses;
        // Keyed by BufferHandle.index. Tracks the access state each buffer is in at the
        // most recent point of pass execution. Persists across frames (the GPU state does).
        std::unordered_map<uint32_t, LastAccessState> mLastAccess;
    };
} // namespace RAnimation
