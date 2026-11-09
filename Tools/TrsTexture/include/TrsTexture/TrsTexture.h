#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "TrsTextureFile.h"

namespace RAnimation
{
    // One node's local TRS as the baker / CPU reference sampler sees it. Layout-identical to the sample's
    // AnimGraph Transform; kept separate so this library depends on glm only.
    struct TrsNodeTransform
    {
        glm::vec3 T = glm::vec3(0.0f);
        glm::quat R = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 S = glm::vec3(1.0f);
    };

    // One clip to bake. The sampler callback produces the full local pose at an absolute clip time in
    // seconds (the caller owns the keyframe data and the sampling code, see Model/ClipSampling.h).
    struct TrsBakeClipDesc
    {
        std::string name;
        float durationFrames = 0.0f;
        float ticksPerSecond = 24.0f;
    };

    using TrsPoseSampler = std::function<void(uint32_t clipIndex, float timeSec, TrsNodeTransform* outNodes)>;

    // Baked TRS texture: RGBA16F Texture2D, one row per sample, three texels per node (T / R / S), values are
    // the node's local (relative to parent) TRS.
    //
    //   width  = nodeCount * 3 texels              height = sum of the clips' row counts
    //   u:  0   1   2 | 3   4   5 | ...            rows FirstRow_c .. FirstRow_c + FrameCount_c - 1 belong to clip c
    //      T0  R0  S0 | T1  R1  S1 | ...           row k samples t_k = k * DurationSec / (FrameCount - 1)
    //
    // Row counts use integer math: FrameCount = ceil(durationFrames * rate / tps) + 1, so row 0 is t = 0 and
    // the last row is exactly t = DurationSec. Quaternions are made sign-continuous per clip and node
    // (negate when dot(R[k], R[k-1]) < 0) so the GPU's row interpolation never passes through zero.
    // Sample() loads two rows with integer addressing and interpolates itself; never hardware-filter this
    // texture, it is data, not an image.
    class BoneTrsTexture
    {
    public:
        static uint32_t RowCountFor(float durationFrames, float ticksPerSecond, float sampleRate);
        static float DurationSeconds(float durationFrames, float ticksPerSecond);

        bool Bake(uint32_t nodeCount,
                  const std::vector<TrsBakeClipDesc>& clips,
                  float sampleRate,
                  const TrsPoseSampler& sampler,
                  std::string& outError);

        // CPU reference of the GPU SampleTrs: two-row lerp (hemisphere-aligned nlerp for R) or nearest row.
        void Sample(uint32_t clipIndex, float timeSec, bool interpolate, TrsNodeTransform* outNodes) const;

        // Raw texel access (RGBA16F packed as 4 x uint16 per texel, row-major, Width texels per row).
        TrsNodeTransform ReadTexel(uint32_t node, uint32_t row) const;
        void WriteTexel(uint32_t node, uint32_t row, const TrsNodeTransform& value);

        const TrsTexHeader& Header() const { return mHeader; }
        TrsTexHeader& Header() { return mHeader; }
        const std::vector<TrsTexClipRecord>& Clips() const { return mClips; }
        std::vector<TrsTexClipRecord>& Clips() { return mClips; }
        const std::vector<uint16_t>& Texels() const { return mTexels; }
        std::vector<uint16_t>& Texels() { return mTexels; }

        uint32_t Width() const { return mHeader.Width; }
        uint32_t Height() const { return mHeader.Height; }
        uint32_t NumNodes() const { return mHeader.NumNodes; }
        uint32_t NumClips() const { return mHeader.NumClips; }
        size_t TexelBytes() const { return mTexels.size() * sizeof(uint16_t); }

        void Reset();

    private:
        TrsTexHeader mHeader{};
        std::vector<TrsTexClipRecord> mClips;
        std::vector<uint16_t> mTexels; // Width * Height * 4 halfs
    };
} // namespace RAnimation
