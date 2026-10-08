#include <Model/ClipSampling.h>

namespace RAnimation
{
    ChannelToNodeTable BuildChannelToNodeTable(const AnimClip& clip,
                                               const std::unordered_map<std::string, int32_t>& nodeIndexByName)
    {
        const auto& channels = clip.GetChannels();
        ChannelToNodeTable table(channels.size(), -1);
        for (size_t i = 0; i < channels.size(); ++i)
        {
            const auto it = nodeIndexByName.find(channels[i]->GetTargetNodeName());
            table[i] = it != nodeIndexByName.end() ? it->second : -1;
        }
        return table;
    }

    void SampleClipPose(const std::vector<Transform>& bindPose,
                        const AnimClip& clip,
                        const ChannelToNodeTable& channelToNode,
                        float timeFrames,
                        Transform* outPose)
    {
        for (size_t n = 0; n < bindPose.size(); ++n)
        {
            outPose[n] = bindPose[n];
        }

        const auto& channels = clip.GetChannels();
        for (size_t c = 0; c < channels.size() && c < channelToNode.size(); ++c)
        {
            const int32_t nodeIndex = channelToNode[c];
            if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= bindPose.size())
            {
                continue;
            }

            const AnimChannel& channel = *channels[c];
            Transform& out = outPose[nodeIndex];
            if (channel.HasRotationKeys())
            {
                out.R = channel.GetRotation(timeFrames);
            }
            if (channel.HasScalingKeys())
            {
                out.S = channel.GetScaling(timeFrames);
            }
            if (channel.HasTranslationKeys())
            {
                out.T = channel.GetTranslation(timeFrames);
            }
        }
    }
} // namespace RAnimation
