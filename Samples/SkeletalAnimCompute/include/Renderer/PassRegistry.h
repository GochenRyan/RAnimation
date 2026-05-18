#pragma once

#include <memory>
#include <utility>
#include <vector>

#include <Renderer/IRenderPass.h>

namespace RAnimation
{
    // Owns all IRenderPass instances. Drives their lifecycle and per-phase Record().
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

        bool DeclareResources(RenderResourceRegistry& registry);
        bool CreatePipelines(RenderContext& context);
        DescriptorPoolRequirements GetDescriptorPoolRequirements(uint32_t queuedFrameNum) const;
        bool CreateDescriptors(FrameContext& context);

        // Run all passes whose GetPhase() matches `phase`, in registration order.
        void RecordPhase(CommandContext& context, RenderPassPhase phase);

        void Cleanup(RRenderData& renderData);

    private:
        std::vector<std::unique_ptr<IRenderPass>> mPasses;
    };
} // namespace RAnimation
