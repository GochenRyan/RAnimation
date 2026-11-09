#include <TrsTexture/TrsTexture.h>

#include <algorithm>
#include <cmath>

#include <glm/gtc/packing.hpp>

namespace RAnimation
{
    namespace
    {
        uint16_t ToHalf(float v)
        {
            return glm::packHalf1x16(v);
        }

        float FromHalf(uint16_t v)
        {
            return glm::unpackHalf1x16(v);
        }

        glm::quat AlignHemisphere(const glm::quat& reference, const glm::quat& q)
        {
            return glm::dot(reference, q) < 0.0f ? -q : q;
        }
    } // namespace

    float BoneTrsTexture::DurationSeconds(float durationFrames, float ticksPerSecond)
    {
        return ticksPerSecond > 0.0f ? durationFrames / ticksPerSecond : 0.0f;
    }

    uint32_t BoneTrsTexture::RowCountFor(float durationFrames, float ticksPerSecond, float sampleRate)
    {
        if (durationFrames <= 0.0f || ticksPerSecond <= 0.0f || sampleRate <= 0.0f)
        {
            return 1;
        }
        // ceil(durationFrames * rate / tps) + 1, with a small epsilon so exact integers do not round up
        const double rows = static_cast<double>(durationFrames) * sampleRate / ticksPerSecond;
        return static_cast<uint32_t>(std::ceil(rows - 1e-6)) + 1u;
    }

    void BoneTrsTexture::Reset()
    {
        mHeader = TrsTexHeader{};
        mClips.clear();
        mTexels.clear();
    }

    bool BoneTrsTexture::Bake(uint32_t nodeCount,
                              const std::vector<TrsBakeClipDesc>& clips,
                              float sampleRate,
                              const TrsPoseSampler& sampler,
                              std::string& outError)
    {
        Reset();
        if (nodeCount == 0)
        {
            outError = "bake: node count is zero";
            return false;
        }
        if (clips.empty())
        {
            outError = "bake: no clips";
            return false;
        }
        if (sampleRate <= 0.0f)
        {
            outError = "bake: sample rate must be positive";
            return false;
        }

        mHeader.Version = kTrsTexVersion;
        mHeader.Layout = 0;
        mHeader.NumNodes = nodeCount;
        mHeader.NumClips = static_cast<uint32_t>(clips.size());
        mHeader.SampleRate = sampleRate;
        mHeader.Width = nodeCount * 3u;

        uint32_t row = 0;
        mClips.resize(clips.size());
        for (size_t c = 0; c < clips.size(); ++c)
        {
            TrsTexClipRecord& rec = mClips[c];
            rec.FirstRow = row;
            rec.FrameCount = RowCountFor(clips[c].durationFrames, clips[c].ticksPerSecond, sampleRate);
            rec.DurationSec = DurationSeconds(clips[c].durationFrames, clips[c].ticksPerSecond);
            rec.RowsPerSec = (rec.FrameCount > 1 && rec.DurationSec > 0.0f)
                                     ? static_cast<float>(rec.FrameCount - 1) / rec.DurationSec
                                     : 0.0f;
            row += rec.FrameCount;
        }
        mHeader.Height = row;

        if (mHeader.Width > 65535u || mHeader.Height > 65535u)
        {
            outError = "bake: texture dimensions exceed 65535";
            return false;
        }

        mTexels.assign(static_cast<size_t>(mHeader.Width) * mHeader.Height * 4u, 0);

        std::vector<TrsNodeTransform> pose(nodeCount);
        std::vector<glm::quat> previousRotation(nodeCount);

        for (size_t c = 0; c < clips.size(); ++c)
        {
            const TrsTexClipRecord& rec = mClips[c];
            for (uint32_t k = 0; k < rec.FrameCount; ++k)
            {
                const float t = rec.FrameCount > 1
                                        ? static_cast<float>(k) * rec.DurationSec / static_cast<float>(rec.FrameCount - 1)
                                        : 0.0f;
                sampler(static_cast<uint32_t>(c), t, pose.data());

                for (uint32_t n = 0; n < nodeCount; ++n)
                {
                    TrsNodeTransform value = pose[n];
                    value.R = glm::normalize(value.R);
                    if (k > 0)
                    {
                        value.R = AlignHemisphere(previousRotation[n], value.R);
                    }
                    previousRotation[n] = value.R;
                    WriteTexel(n, rec.FirstRow + k, value);
                }
            }
        }

        return true;
    }

    void BoneTrsTexture::WriteTexel(uint32_t node, uint32_t row, const TrsNodeTransform& value)
    {
        const size_t base = (static_cast<size_t>(row) * mHeader.Width + static_cast<size_t>(node) * 3u) * 4u;
        uint16_t* t = &mTexels[base];
        t[0] = ToHalf(value.T.x);
        t[1] = ToHalf(value.T.y);
        t[2] = ToHalf(value.T.z);
        t[3] = ToHalf(0.0f);
        t[4] = ToHalf(value.R.x);
        t[5] = ToHalf(value.R.y);
        t[6] = ToHalf(value.R.z);
        t[7] = ToHalf(value.R.w);
        t[8] = ToHalf(value.S.x);
        t[9] = ToHalf(value.S.y);
        t[10] = ToHalf(value.S.z);
        t[11] = ToHalf(0.0f);
    }

    TrsNodeTransform BoneTrsTexture::ReadTexel(uint32_t node, uint32_t row) const
    {
        TrsNodeTransform value;
        const size_t base = (static_cast<size_t>(row) * mHeader.Width + static_cast<size_t>(node) * 3u) * 4u;
        const uint16_t* t = &mTexels[base];
        value.T = glm::vec3(FromHalf(t[0]), FromHalf(t[1]), FromHalf(t[2]));
        value.R = glm::quat(FromHalf(t[7]), FromHalf(t[4]), FromHalf(t[5]), FromHalf(t[6])); // ctor is w,x,y,z
        value.S = glm::vec3(FromHalf(t[8]), FromHalf(t[9]), FromHalf(t[10]));
        return value;
    }

    void BoneTrsTexture::Sample(uint32_t clipIndex, float timeSec, bool interpolate, TrsNodeTransform* outNodes) const
    {
        if (clipIndex >= mClips.size())
        {
            return;
        }
        const TrsTexClipRecord& rec = mClips[clipIndex];
        const float t = std::clamp(timeSec, 0.0f, rec.DurationSec);
        const float row = t * rec.RowsPerSec;
        const uint32_t last = rec.FrameCount - 1;
        const uint32_t r0 = std::min(static_cast<uint32_t>(std::floor(row)), last);
        const uint32_t r1 = std::min(r0 + 1, last);
        const float exactFrac = row - static_cast<float>(r0);
        const float frac = interpolate ? exactFrac : (exactFrac >= 0.5f ? 1.0f : 0.0f);

        for (uint32_t n = 0; n < mHeader.NumNodes; ++n)
        {
            const TrsNodeTransform a = ReadTexel(n, rec.FirstRow + r0);
            const TrsNodeTransform b = ReadTexel(n, rec.FirstRow + r1);
            TrsNodeTransform& out = outNodes[n];
            out.T = glm::mix(a.T, b.T, frac);
            out.S = glm::mix(a.S, b.S, frac);
            const glm::quat qb = AlignHemisphere(a.R, b.R);
            out.R = glm::normalize(a.R * (1.0f - frac) + qb * frac);
        }
    }
} // namespace RAnimation
