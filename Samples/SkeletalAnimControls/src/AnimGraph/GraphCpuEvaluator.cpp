#include <AnimGraph/GraphCpuEvaluator.h>

#include <AnimGraph/AnimGraphInstance.h>
#include <AnimGraph/GraphRunner.h>
#include <MemStack/MemStack.h>
#include <Model/Model.h>

namespace RAnimation
{
    GraphCpuEvaluator::GraphCpuEvaluator(AnimGraphInstance& instance) : mInstance(instance)
    {
        if (mInstance.Baked() == nullptr)
        {
            mError = "graph is not baked";
        }
    }

    void GraphCpuEvaluator::Tick(float deltaTime, HybridWorkCpu& out)
    {
        out.Clear();
        out.nodeCount = mInstance.NodeCount();
        if (!Usable())
        {
            return;
        }

        GraphRunner::Update(mInstance, deltaTime);
        GraphRunner::DrainCachedPoses(mInstance);

        Pose pose;
        pose.bones = out.AllocateSlot();
        pose.count = out.nodeCount;
        GraphRunner::EvaluateRoot(mInstance, pose);

        out.terms.push_back({true, 0, 0.0f, 1.0f});
    }

    CostModel GraphCpuEvaluator::Cost() const
    {
        CostModel cost;
        const Model* model = mInstance.GetModel();
        if (model != nullptr)
        {
            size_t keyBytes = 0;
            for (const auto& clip : model->GetAnimClips())
            {
                for (const auto& channel : clip->GetChannels())
                {
                    keyBytes += channel->GetKeyCount() * (sizeof(float) + sizeof(glm::vec4));
                }
            }
            cost.ResidentBytes = keyBytes;
        }
        cost.SlotUploadBytes = static_cast<size_t>(mInstance.NodeCount()) * 48u;
        cost.CpuPosesPerFrame = 1;
        cost.GpuTerms = 1;
        return cost;
    }
} // namespace RAnimation
