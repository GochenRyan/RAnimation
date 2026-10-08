#include <Model/TrsSource.h>

#include <filesystem>

#include <Model/TransformDecompose.h>

namespace RAnimation
{
    TrsSourceView BuildTrsSourceView(const UsdLoadedModel& loaded)
    {
        TrsSourceView view;
        view.nodes.reserve(loaded.nodes.size());
        for (const UsdNodeData& node : loaded.nodes)
        {
            TrsSourceNode n;
            n.name = node.name;
            n.parentIndex = node.parentIndex;
            const float* m = &node.localTransform[0][0];
            for (int i = 0; i < 16; ++i)
            {
                n.localTransform[i] = m[i];
            }
            view.nodes.emplace_back(std::move(n));
        }

        view.clips.reserve(loaded.animClips.size());
        for (const UsdAnimClipData& clip : loaded.animClips)
        {
            TrsSourceClip c;
            c.name = clip.name;
            c.durationFrames = clip.duration;
            c.ticksPerSecond = clip.ticksPerSecond;
            for (const UsdAnimChannelData& ch : clip.channels)
            {
                c.channelNodeNames.push_back(ch.nodeName);

                c.keyTimes.insert(c.keyTimes.end(), ch.translationTimings.begin(), ch.translationTimings.end());
                c.keyTimes.insert(c.keyTimes.end(), ch.rotationTimings.begin(), ch.rotationTimings.end());
                c.keyTimes.insert(c.keyTimes.end(), ch.scaleTimings.begin(), ch.scaleTimings.end());

                for (const glm::vec3& v : ch.translations)
                {
                    c.keyValues.insert(c.keyValues.end(), {v.x, v.y, v.z});
                }
                for (const glm::quat& q : ch.rotations)
                {
                    c.keyValues.insert(c.keyValues.end(), {q.x, q.y, q.z, q.w});
                }
                for (const glm::vec3& v : ch.scalings)
                {
                    c.keyValues.insert(c.keyValues.end(), {v.x, v.y, v.z});
                }
            }
            view.clips.emplace_back(std::move(c));
        }
        return view;
    }

    std::vector<Transform> BuildBindPose(const UsdLoadedModel& loaded)
    {
        std::vector<Transform> bind(loaded.nodes.size());
        for (size_t i = 0; i < loaded.nodes.size(); ++i)
        {
            DecomposeBindTransform(loaded.nodes[i].localTransform, bind[i]);
        }
        return bind;
    }

    std::vector<std::shared_ptr<AnimClip>> BuildAnimClips(const UsdLoadedModel& loaded)
    {
        std::vector<std::shared_ptr<AnimClip>> clips;
        clips.reserve(loaded.animClips.size());
        for (const UsdAnimClipData& clipData : loaded.animClips)
        {
            std::shared_ptr<AnimClip> animClip = std::make_shared<AnimClip>();
            animClip->SetClipName(clipData.name);
            animClip->SetClipDuration(clipData.duration);
            animClip->SetClipTicksPerSecond(clipData.ticksPerSecond);
            animClip->SetClipLoop(clipData.loop);
            for (const UsdAnimChannelData& ch : clipData.channels)
            {
                std::shared_ptr<AnimChannel> channel = std::make_shared<AnimChannel>();
                channel->SetChannelData(ch.nodeName, ch.translationTimings, ch.translations, ch.rotationTimings,
                                        ch.rotations, ch.scaleTimings, ch.scalings, ch.preState, ch.postState);
                animClip->AddChannel(channel);
            }
            clips.emplace_back(std::move(animClip));
        }
        return clips;
    }

    std::vector<TrsBakeClipDesc> BuildBakeClipDescs(const UsdLoadedModel& loaded)
    {
        std::vector<TrsBakeClipDesc> descs;
        descs.reserve(loaded.animClips.size());
        for (const UsdAnimClipData& clip : loaded.animClips)
        {
            TrsBakeClipDesc d;
            d.name = clip.name;
            d.durationFrames = clip.duration;
            d.ticksPerSecond = clip.ticksPerSecond > 0.0f ? clip.ticksPerSecond : 24.0f;
            descs.emplace_back(std::move(d));
        }
        return descs;
    }

    std::string TrsTexturePathForAsset(const std::string& assetUsdPath)
    {
        const std::filesystem::path asset(assetUsdPath);
        std::string stem = asset.filename().generic_string();
        for (const char* suffix : {".asset.usda", ".asset.usd", ".asset.usdc", ".usda", ".usd", ".usdc"})
        {
            const std::string s = suffix;
            if (stem.size() > s.size() && stem.compare(stem.size() - s.size(), s.size(), s) == 0)
            {
                stem.erase(stem.size() - s.size());
                break;
            }
        }
        return (asset.parent_path() / "baked" / (stem + ".trstex")).generic_string();
    }
} // namespace RAnimation
