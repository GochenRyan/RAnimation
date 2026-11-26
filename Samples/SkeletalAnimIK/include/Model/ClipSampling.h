#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <AnimGraph/Pose.h>
#include <Model/AnimClip.h>

namespace RAnimation
{
    // Maps a clip's channels onto node indices (Model::GetNodeList() order). -1 = channel targets no node.
    using ChannelToNodeTable = std::vector<int32_t>;

    ChannelToNodeTable BuildChannelToNodeTable(const AnimClip& clip,
                                               const std::unordered_map<std::string, int32_t>& nodeIndexByName);

    // The single clip-sampling routine shared by the runtime (ClipPlayer, implicit single-clip graphs, the
    // camera pose) and the TrsTextureBaker. Semantics are exactly those of the original
    // ModelInstance::UpdateAnimationState: start from the bind pose, then overwrite only the components a
    // channel actually has keys for. timeFrames is in clip ticks/frames (the AnimChannel time base).
    // outPose must hold bindPose.size() entries. No Node/Model dependency on purpose.
    void SampleClipPose(const std::vector<Transform>& bindPose,
                        const AnimClip& clip,
                        const ChannelToNodeTable& channelToNode,
                        float timeFrames,
                        Transform* outPose);

    // Seconds <-> frames helpers so every caller converts the same way.
    inline float ClipDurationSeconds(const AnimClip& clip)
    {
        const float tps = clip.GetClipTicksPerSecond();
        return tps > 0.0f ? clip.GetClipDuration() / tps : 0.0f;
    }

    inline float ClipSecondsToFrames(const AnimClip& clip, float seconds)
    {
        return seconds * clip.GetClipTicksPerSecond();
    }
} // namespace RAnimation
