// TrsTextureBaker: bakes a UsdSkel asset's clips into a .trstex (RGBA16F TRS texture + clip table).
//
//   TrsTextureBaker <Asset.asset.usda> [-o out.trstex] [--rate 30] [--force] [--check] [--selftest]
//
// Default output is <asset dir>/baked/<Stem>.trstex. The bake is fingerprint-gated: when the existing file
// was baked from identical source data (and the same rate/version/layout) the tool prints "up to date" and
// exits 0 without touching the file. --check only validates (and prints the resampling error against the
// CPU sampler), --force always rewrites, --selftest runs the format/formula tests and exits.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <fmt/base.h>
#include <fmt/color.h>
#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <AnimGraph/Pose.h>
#include <Model/ClipSampling.h>
#include <Model/TrsSource.h>
#include <Model/UsdModelLoader.h>
#include <TrsTexture/TrsTexture.h>
#include <TrsTexture/TrsTextureFile.h>

using namespace RAnimation;

namespace
{
    struct Options
    {
        std::string assetPath;
        std::string outPath;
        float rate = kTrsTexDefaultSampleRate;
        bool force = false;
        bool check = false;
        bool selftest = false;
    };

    void PrintUsage()
    {
        fmt::print("usage: TrsTextureBaker <Asset.asset.usda> [-o out.trstex] [--rate 30] [--force] [--check] [--selftest]\n");
    }

    bool ParseArgs(int argc, char** argv, Options& o)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string a = argv[i];
            if (a == "-o" && i + 1 < argc)
            {
                o.outPath = argv[++i];
            }
            else if (a == "--rate" && i + 1 < argc)
            {
                o.rate = static_cast<float>(std::atof(argv[++i]));
            }
            else if (a == "--force")
            {
                o.force = true;
            }
            else if (a == "--check")
            {
                o.check = true;
            }
            else if (a == "--selftest")
            {
                o.selftest = true;
            }
            else if (a == "-h" || a == "--help")
            {
                return false;
            }
            else if (o.assetPath.empty())
            {
                o.assetPath = a;
            }
            else
            {
                fmt::print(stderr, "unknown argument '{}'\n", a);
                return false;
            }
        }
        return o.selftest || !o.assetPath.empty();
    }

    // Sample-side clip sampler used both for baking and as the reference in --check.
    struct ClipSampler
    {
        std::vector<Transform> bindPose;
        std::vector<std::shared_ptr<AnimClip>> clips;
        std::vector<ChannelToNodeTable> tables;
        std::vector<Transform> scratch;

        void Sample(uint32_t clipIndex, float timeSec, Transform* out)
        {
            const AnimClip& clip = *clips[clipIndex];
            SampleClipPose(bindPose, clip, tables[clipIndex], ClipSecondsToFrames(clip, timeSec), out);
        }
    };

    float QuatAngleDegrees(const glm::quat& a, const glm::quat& b)
    {
        const float d = std::min(1.0f, std::fabs(glm::dot(glm::normalize(a), glm::normalize(b))));
        return glm::degrees(2.0f * std::acos(d));
    }

    int RunSelfTest()
    {
        int failures = 0;
        auto expect = [&](bool ok, const char* what)
        {
            fmt::print("{} {}\n", ok ? "[ ok ]" : "[FAIL]", what);
            failures += ok ? 0 : 1;
        };

        // Row-count formula: FrameCount = ceil(durFrames * rate / tps) + 1
        expect(BoneTrsTexture::RowCountFor(16.0f, 24.0f, 30.0f) == 21, "16 frames @24 -> 30 Hz = 21 rows");
        expect(BoneTrsTexture::RowCountFor(200.0f, 24.0f, 30.0f) == 251, "200 frames @24 -> 30 Hz = 251 rows");
        expect(BoneTrsTexture::RowCountFor(28.0f, 24.0f, 24.0f) == 29, "28 frames @24 -> 24 Hz = 29 rows (1:1 keys)");
        expect(BoneTrsTexture::RowCountFor(0.0f, 24.0f, 30.0f) == 1, "0 frames -> 1 row");

        // Synthetic bake: 3 nodes, 2 clips; node n rotates about Y by t*90deg, translates by (n, t, 0)
        BoneTrsTexture tex;
        std::vector<TrsBakeClipDesc> clips = {{"a", 24.0f, 24.0f}, {"b", 12.0f, 24.0f}};
        std::string err;
        const bool baked = tex.Bake(
                3,
                clips,
                30.0f,
                [](uint32_t, float t, TrsNodeTransform* out)
                {
                    for (uint32_t n = 0; n < 3; ++n)
                    {
                        out[n].T = glm::vec3(static_cast<float>(n), t, 0.0f);
                        out[n].R = glm::angleAxis(glm::radians(90.0f * t), glm::vec3(0, 1, 0));
                        out[n].S = glm::vec3(1.0f + 0.5f * t);
                    }
                },
                err);
        expect(baked, "synthetic bake succeeds");
        expect(tex.Width() == 9 && tex.Height() == 31 + 16, "synthetic bake dimensions 9 x 47");
        expect(tex.Clips()[1].FirstRow == 31 && std::fabs(tex.Clips()[1].RowsPerSec - 30.0f) < 1e-4f,
               "clip table rows / rows-per-second");

        // Last row sits exactly at the end of the clip
        {
            TrsNodeTransform v = tex.ReadTexel(1, tex.Clips()[0].FirstRow + tex.Clips()[0].FrameCount - 1);
            expect(std::fabs(v.T.y - 1.0f) < 2e-3f, "last row samples t = duration");
        }

        // Sample() reproduces the analytic function between rows (half precision tolerance)
        {
            TrsNodeTransform out[3];
            tex.Sample(0, 0.5f, true, out);
            const glm::quat expected = glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 1, 0));
            expect(std::fabs(out[2].T.y - 0.5f) < 2e-3f && QuatAngleDegrees(out[2].R, expected) < 0.5f,
                   "Sample() interpolates between rows");
        }

        // Save / load round trip with fingerprint
        {
            TrsSourceView view;
            view.nodes.resize(3);
            view.nodes[0].name = "root";
            view.nodes[1].name = "a";
            view.nodes[1].parentIndex = 0;
            view.nodes[2].name = "b";
            view.nodes[2].parentIndex = 1;
            view.clips.resize(2);
            view.clips[0].name = "a";
            view.clips[0].keyValues = {1.0f, 2.0f, 3.0f};
            view.clips[1].name = "b";
            const uint64_t fp1 = ComputeTrsSourceFingerprint(view, kTrsTexVersion, 0, 30.0f);
            const uint64_t fp2 = ComputeTrsSourceFingerprint(view, kTrsTexVersion, 0, 30.0f);
            view.clips[0].keyValues[1] = 2.0001f;
            const uint64_t fp3 = ComputeTrsSourceFingerprint(view, kTrsTexVersion, 0, 30.0f);
            expect(fp1 == fp2 && fp1 != fp3 && fp1 != 0, "fingerprint is stable and value-sensitive");

            tex.Header().Fingerprint = fp1;
            const std::string path = (std::filesystem::temp_directory_path() / "trstex_selftest.trstex").generic_string();
            TrsTexIoResult saved = SaveTrsTex(path, tex);
            expect(saved.bOk, saved.bOk ? "save" : saved.Error.c_str());

            BoneTrsTexture loaded;
            TrsTexIoResult ok = LoadTrsTex(path, loaded, fp1);
            expect(ok.bOk && loaded.Texels() == tex.Texels() && loaded.Clips().size() == 2, "load round trip");
            TrsTexIoResult stale = LoadTrsTex(path, loaded, fp3);
            expect(!stale.bOk, "stale fingerprint is rejected");
            std::filesystem::remove(path);
        }

        fmt::print("{}\n", failures == 0 ? "selftest passed" : "selftest FAILED");
        return failures == 0 ? 0 : 1;
    }

    int RunCheck(const std::string& outPath, const UsdLoadedModel& loaded, ClipSampler& sampler)
    {
        TrsTexHeader header;
        TrsTexIoResult r = ReadTrsTexHeader(outPath, header);
        if (!r.bOk)
        {
            fmt::print(stderr, fg(fmt::color::red), "{}\n", r.Error);
            return 1;
        }
        const TrsSourceView view = BuildTrsSourceView(loaded);
        const uint64_t expected = ComputeTrsSourceFingerprint(view, header.Version, header.Layout, header.SampleRate);
        BoneTrsTexture tex;
        r = LoadTrsTex(outPath, tex, expected);
        if (!r.bOk)
        {
            fmt::print(stderr, fg(fmt::color::red), "{}\n", r.Error);
            return 1;
        }

        fmt::print("{}: {}x{} RGBA16F, {} nodes, {} clips, {:.1f} Hz, fingerprint {:016x}\n",
                   outPath,
                   tex.Width(),
                   tex.Height(),
                   tex.NumNodes(),
                   tex.NumClips(),
                   header.SampleRate,
                   header.Fingerprint);

        // Resampling error: compare the texture sampler against the CPU clip sampler on a fine time grid
        // (8 steps per source frame), i.e. the worst case the runtime can hit between keys.
        constexpr int kSubSteps = 8;
        std::vector<Transform> ref(tex.NumNodes());
        std::vector<TrsNodeTransform> tx(tex.NumNodes());
        fmt::print("{:<22} {:>6} {:>9} {:>10} {:>10} {:>10}\n", "clip", "rows", "dur[s]", "maxT[m]", "maxR[deg]", "maxS");
        for (uint32_t c = 0; c < tex.NumClips(); ++c)
        {
            const TrsTexClipRecord& rec = tex.Clips()[c];
            const AnimClip& clip = *sampler.clips[c];
            float maxT = 0.0f, maxR = 0.0f, maxS = 0.0f;
            const int steps = static_cast<int>(clip.GetClipDuration()) * kSubSteps;
            for (int f = 0; f <= steps; ++f)
            {
                const float tSec = static_cast<float>(f) / (clip.GetClipTicksPerSecond() * static_cast<float>(kSubSteps));
                sampler.Sample(c, tSec, ref.data());
                tex.Sample(c, tSec, true, tx.data());
                for (uint32_t n = 0; n < tex.NumNodes(); ++n)
                {
                    maxT = std::max(maxT, glm::length(ref[n].T - tx[n].T));
                    maxR = std::max(maxR, QuatAngleDegrees(ref[n].R, tx[n].R));
                    maxS = std::max(maxS, glm::length(ref[n].S - tx[n].S));
                }
            }
            fmt::print("{:<22} {:>6} {:>9.3f} {:>10.5f} {:>10.3f} {:>10.5f}\n",
                       clip.GetClipName(),
                       rec.FrameCount,
                       rec.DurationSec,
                       maxT,
                       maxR,
                       maxS);
        }
        fmt::print("check passed\n");
        return 0;
    }
} // namespace

int main(int argc, char** argv)
{
    Options opt;
    if (!ParseArgs(argc, argv, opt))
    {
        PrintUsage();
        return 2;
    }
    if (opt.selftest)
    {
        return RunSelfTest();
    }
    if (opt.rate <= 0.0f)
    {
        fmt::print(stderr, "--rate must be positive\n");
        return 2;
    }

    UsdLoadedModel loaded;
    if (!LoadUsdModel(opt.assetPath, loaded))
    {
        fmt::print(stderr, fg(fmt::color::red), "could not load '{}'\n", opt.assetPath);
        return 1;
    }
    if (loaded.animClips.empty())
    {
        fmt::print(stderr, fg(fmt::color::red), "'{}' has no animation clips; nothing to bake\n", opt.assetPath);
        return 1;
    }

    const std::string outPath = opt.outPath.empty() ? TrsTexturePathForAsset(opt.assetPath) : opt.outPath;

    ClipSampler sampler;
    sampler.bindPose = BuildBindPose(loaded);
    sampler.clips = BuildAnimClips(loaded);
    std::unordered_map<std::string, int32_t> nodeIndexByName;
    for (size_t i = 0; i < loaded.nodes.size(); ++i)
    {
        nodeIndexByName[loaded.nodes[i].name] = static_cast<int32_t>(i);
    }
    for (const auto& clip : sampler.clips)
    {
        sampler.tables.emplace_back(BuildChannelToNodeTable(*clip, nodeIndexByName));
    }

    if (opt.check)
    {
        return RunCheck(outPath, loaded, sampler);
    }

    const TrsSourceView view = BuildTrsSourceView(loaded);
    const uint64_t fingerprint = ComputeTrsSourceFingerprint(view, kTrsTexVersion, 0, opt.rate);

    if (!opt.force && std::filesystem::exists(outPath))
    {
        TrsTexHeader existing;
        if (ReadTrsTexHeader(outPath, existing).bOk && existing.Fingerprint == fingerprint)
        {
            fmt::print("{}: up to date (fingerprint {:016x})\n", outPath, fingerprint);
            return 0;
        }
    }

    BoneTrsTexture tex;
    std::string err;
    const uint32_t nodeCount = static_cast<uint32_t>(loaded.nodes.size());
    std::vector<Transform> scratch(nodeCount);
    const bool baked = tex.Bake(
            nodeCount,
            BuildBakeClipDescs(loaded),
            opt.rate,
            [&](uint32_t clipIndex, float timeSec, TrsNodeTransform* out)
            {
                sampler.Sample(clipIndex, timeSec, scratch.data());
                for (uint32_t n = 0; n < nodeCount; ++n)
                {
                    out[n].T = scratch[n].T;
                    out[n].R = scratch[n].R;
                    out[n].S = scratch[n].S;
                }
            },
            err);
    if (!baked)
    {
        fmt::print(stderr, fg(fmt::color::red), "bake failed: {}\n", err);
        return 1;
    }
    tex.Header().Fingerprint = fingerprint;

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(outPath).parent_path(), ec);
    const TrsTexIoResult saved = SaveTrsTex(outPath, tex);
    if (!saved.bOk)
    {
        fmt::print(stderr, fg(fmt::color::red), "{}\n", saved.Error);
        return 1;
    }

    fmt::print("{}: baked {}x{} RGBA16F ({} nodes, {} clips, {:.1f} Hz, {} KB), fingerprint {:016x}\n",
               outPath,
               tex.Width(),
               tex.Height(),
               tex.NumNodes(),
               tex.NumClips(),
               opt.rate,
               tex.TexelBytes() / 1024,
               fingerprint);
    return 0;
}
