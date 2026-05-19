#include <Renderer/PassRegistry.h>

#include <Renderer/RenderResourceRegistry.h>

namespace RAnimation
{
    bool PassRegistry::DeclareResources(ResourceContext& context)
    {
        for (auto& pass : mPasses)
        {
            if (!pass->DeclareResources(context))
            {
                return false;
            }
        }
        return true;
    }

    bool PassRegistry::CreatePipelines(RenderContext& context)
    {
        for (auto& pass : mPasses)
        {
            if (!pass->CreatePipeline(context))
            {
                return false;
            }
        }
        return true;
    }

    DescriptorPoolRequirements PassRegistry::GetDescriptorPoolRequirements(uint32_t queuedFrameNum) const
    {
        DescriptorPoolRequirements total = {};
        for (const auto& pass : mPasses)
        {
            total += pass->GetDescriptorPoolRequirements(queuedFrameNum);
        }
        return total;
    }

    bool PassRegistry::CreateDescriptors(FrameContext& context)
    {
        for (auto& pass : mPasses)
        {
            if (!pass->CreateDescriptors(context))
            {
                return false;
            }
        }
        return true;
    }

    void PassRegistry::RecordPhase(CommandContext& context, RenderPassPhase phase)
    {
        for (auto& pass : mPasses)
        {
            if (pass->GetPhase() != phase)
            {
                continue;
            }
            EmitBarriersForPass(context, *pass);
            pass->Record(context);
        }
    }

    void PassRegistry::EmitBarriersForPass(CommandContext& context, IRenderPass& pass)
    {
        RegistryAccessBuilder builder;
        pass.DeclareAccess(builder);
        const std::vector<ResourceAccess>& declared = builder.Accesses();
        if (declared.empty())
        {
            return;
        }

        std::vector<nri::BufferBarrierDesc> bufferBarriers;
        bufferBarriers.reserve(declared.size());

        for (const ResourceAccess& access : declared)
        {
            LastAccessState& last = mLastAccess[access.handle.index];

            // Skip when the buffer is already in the requested state.
            if (last.initialized && last.access == access.access && last.stage == access.stage)
            {
                continue;
            }

            nri::Buffer* buffer = context.registry.GetBuffer(access.handle);
            nri::BufferBarrierDesc barrier = {};
            barrier.buffer = buffer;
            barrier.before = {last.access, last.stage};
            barrier.after = {access.access, access.stage};
            bufferBarriers.push_back(barrier);

            last.access = access.access;
            last.stage = access.stage;
            last.initialized = true;
        }

        if (bufferBarriers.empty())
        {
            return;
        }

        nri::BarrierDesc barrierDesc = {};
        barrierDesc.buffers = bufferBarriers.data();
        barrierDesc.bufferNum = static_cast<uint32_t>(bufferBarriers.size());
        context.NRI.CmdBarrier(context.commandBuffer, barrierDesc);
    }

    void PassRegistry::Cleanup(RRenderData& renderData)
    {
        for (auto& pass : mPasses)
        {
            pass->Cleanup(renderData);
        }
        mPasses.clear();
        mLastAccess.clear();
    }
} // namespace RAnimation
