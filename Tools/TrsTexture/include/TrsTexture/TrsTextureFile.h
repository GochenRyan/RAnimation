#pragma once
#include <cstdint>
#include <string>
#include <vector>

// On-disk format of a baked TRS texture (.trstex): header + clip table + raw half4 texels. Cooked at tool
// time, only loaded at runtime. Two things the plain format must keep:
//   1. a version number - a layout change rejects old files instead of misreading them;
//   2. a source fingerprint - an asset re-exported without re-baking can match the header (same node and
//      clip counts) while the texels are stale; the fingerprint turns that into an explicit rejection.
// The fingerprint covers everything that affects the texels: every node's name/parent/bind matrix, every
// clip's name/duration/rate and all keyframe times and values, plus the header's Version/Layout/SampleRate.

namespace RAnimation
{
    constexpr uint32_t kTrsTexVersion = 2;
    constexpr float kTrsTexDefaultSampleRate = 30.0f;

#pragma pack(push, 1)
    struct TrsTexHeader
    {
        char Magic[8] = {'T', 'R', 'S', 'T', 'E', 'X', 0, 0};
        uint32_t Version = kTrsTexVersion;
        uint32_t Width = 0;       // NumNodes * 3 texels (T / R / S per node)
        uint32_t Height = 0;      // sum of all clips' FrameCount
        uint32_t NumNodes = 0;    // node count incl. the synthetic root (Model::GetNodeList().size())
        uint32_t NumClips = 0;
        float SampleRate = 0.0f;  // informational: the rate the baker was asked for (rows/sec per clip is in the record)
        uint32_t Layout = 0;      // 0 = 3 RGBA16F texels per node (T, R, S)
        uint64_t Fingerprint = 0; // see file comment
        uint32_t Reserved = 0;
    };
    static_assert(sizeof(TrsTexHeader) == 48, "TrsTexHeader is a disk format; keep it 48 bytes");

    // Mirrored 1:1 by the HLSL ClipTable StructuredBuffer (stride 16).
    struct TrsTexClipRecord
    {
        uint32_t FirstRow = 0;
        uint32_t FrameCount = 0;   // rows for this clip; row 0 = t 0, row FrameCount-1 = t DurationSec
        float DurationSec = 0.0f;
        float RowsPerSec = 0.0f;   // (FrameCount - 1) / DurationSec, 0 when the clip has a single row
    };
    static_assert(sizeof(TrsTexClipRecord) == 16, "TrsTexClipRecord is a disk/GPU format; keep it 16 bytes");
#pragma pack(pop)

    // ---- source fingerprint -------------------------------------------------------------------------

    struct TrsSourceNode
    {
        std::string name;
        int32_t parentIndex = -1;
        float localTransform[16] = {};
    };

    struct TrsSourceClip
    {
        std::string name;
        float durationFrames = 0.0f;
        float ticksPerSecond = 0.0f;
        std::vector<std::string> channelNodeNames; // channel order
        std::vector<float> keyTimes;  // T, R, S timings concatenated per channel
        std::vector<float> keyValues; // T xyz, R xyzw, S xyz concatenated per channel
    };

    // Plain view of everything that influences the bake. The tool fills it from UsdLoadedModel, the runtime
    // from the same UsdLoadedModel inside Model::LoadModel (see Model/TrsSource.h in the sample).
    struct TrsSourceView
    {
        std::vector<TrsSourceNode> nodes;
        std::vector<TrsSourceClip> clips;
    };

    // FNV-1a 64 over the view plus the header fields that change the texel contents.
    uint64_t ComputeTrsSourceFingerprint(const TrsSourceView& view, uint32_t version, uint32_t layout, float sampleRate);

    // ---- io ------------------------------------------------------------------------------------------

    struct TrsTexIoResult
    {
        bool bOk = false;
        std::string Error;
    };

    class BoneTrsTexture;

    TrsTexIoResult SaveTrsTex(const std::string& path, const BoneTrsTexture& tex);

    // Reads header + clip table + texels. Validates magic, version, layout and sizes. When
    // expectedFingerprint != 0 a mismatch is an error (stale bake).
    TrsTexIoResult LoadTrsTex(const std::string& path, BoneTrsTexture& out, uint64_t expectedFingerprint = 0);
    TrsTexIoResult ReadTrsTexHeader(const std::string& path, TrsTexHeader& outHeader);
} // namespace RAnimation
