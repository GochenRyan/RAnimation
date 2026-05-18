#include <Renderer/PassRegistry.h>

namespace RAnimation
{
    bool PassRegistry::DeclareResources(RenderResourceRegistry& registry)
    {
        for (auto& pass : mPasses)
        {
            if (!pass->DeclareResources(registry))
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
            if (pass->GetPhase() == phase)
            {
                pass->Record(context);
            }
        }
    }

    void PassRegistry::Cleanup(RRenderData& renderData)
    {
        for (auto& pass : mPasses)
        {
            pass->Cleanup(renderData);
        }
        mPasses.clear();
    }
} // namespace RAnimation
