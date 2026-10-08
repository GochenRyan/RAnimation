#include <TrsTexture/TrsTexture.h>
#include <TrsTexture/TrsTextureFile.h>

#include <cstring>
#include <fstream>

#include <fmt/format.h>

namespace RAnimation
{
    namespace
    {
        constexpr uint64_t kFnvOffset = 14695981039346656037ull;
        constexpr uint64_t kFnvPrime = 1099511628211ull;

        struct Fnv1a64
        {
            uint64_t h = kFnvOffset;

            void Bytes(const void* data, size_t size)
            {
                const uint8_t* p = static_cast<const uint8_t*>(data);
                for (size_t i = 0; i < size; ++i)
                {
                    h ^= p[i];
                    h *= kFnvPrime;
                }
            }
            template <typename T>
            void Pod(const T& v)
            {
                Bytes(&v, sizeof(T));
            }
            void Str(const std::string& s)
            {
                const uint32_t len = static_cast<uint32_t>(s.size());
                Pod(len);
                Bytes(s.data(), s.size());
            }
            template <typename T>
            void Vec(const std::vector<T>& v)
            {
                const uint32_t len = static_cast<uint32_t>(v.size());
                Pod(len);
                if (!v.empty())
                {
                    Bytes(v.data(), v.size() * sizeof(T));
                }
            }
        };

        bool MagicMatches(const TrsTexHeader& h)
        {
            static const char kMagic[8] = {'T', 'R', 'S', 'T', 'E', 'X', 0, 0};
            return std::memcmp(h.Magic, kMagic, sizeof(kMagic)) == 0;
        }
    } // namespace

    uint64_t ComputeTrsSourceFingerprint(const TrsSourceView& view, uint32_t version, uint32_t layout, float sampleRate)
    {
        Fnv1a64 f;
        f.Pod(version);
        f.Pod(layout);
        f.Pod(sampleRate);

        f.Pod(static_cast<uint32_t>(view.nodes.size()));
        for (const TrsSourceNode& n : view.nodes)
        {
            f.Str(n.name);
            f.Pod(n.parentIndex);
            f.Bytes(n.localTransform, sizeof(n.localTransform));
        }

        f.Pod(static_cast<uint32_t>(view.clips.size()));
        for (const TrsSourceClip& c : view.clips)
        {
            f.Str(c.name);
            f.Pod(c.durationFrames);
            f.Pod(c.ticksPerSecond);
            f.Pod(static_cast<uint32_t>(c.channelNodeNames.size()));
            for (const std::string& s : c.channelNodeNames)
            {
                f.Str(s);
            }
            f.Vec(c.keyTimes);
            f.Vec(c.keyValues);
        }
        return f.h;
    }

    TrsTexIoResult SaveTrsTex(const std::string& path, const BoneTrsTexture& tex)
    {
        TrsTexIoResult r;
        const TrsTexHeader& h = tex.Header();
        if (tex.Clips().size() != h.NumClips || tex.Texels().size() != static_cast<size_t>(h.Width) * h.Height * 4u)
        {
            r.Error = "save: header does not match payload sizes";
            return r;
        }

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
        {
            r.Error = fmt::format("save: cannot open '{}' for writing", path);
            return r;
        }
        out.write(reinterpret_cast<const char*>(&h), sizeof(h));
        out.write(reinterpret_cast<const char*>(tex.Clips().data()), tex.Clips().size() * sizeof(TrsTexClipRecord));
        out.write(reinterpret_cast<const char*>(tex.Texels().data()), tex.Texels().size() * sizeof(uint16_t));
        if (!out.good())
        {
            r.Error = fmt::format("save: write failed for '{}'", path);
            return r;
        }
        r.bOk = true;
        return r;
    }

    TrsTexIoResult ReadTrsTexHeader(const std::string& path, TrsTexHeader& outHeader)
    {
        TrsTexIoResult r;
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open())
        {
            r.Error = fmt::format("load: cannot open '{}'", path);
            return r;
        }
        in.read(reinterpret_cast<char*>(&outHeader), sizeof(outHeader));
        if (!in.good())
        {
            r.Error = fmt::format("load: '{}' is too short for a header", path);
            return r;
        }
        if (!MagicMatches(outHeader))
        {
            r.Error = fmt::format("load: '{}' is not a .trstex file (bad magic)", path);
            return r;
        }
        if (outHeader.Version != kTrsTexVersion)
        {
            r.Error = fmt::format("load: '{}' has version {} but the runtime requires {}",
                                  path,
                                  outHeader.Version,
                                  kTrsTexVersion);
            return r;
        }
        if (outHeader.Layout != 0)
        {
            r.Error = fmt::format("load: '{}' uses unsupported layout {}", path, outHeader.Layout);
            return r;
        }
        r.bOk = true;
        return r;
    }

    TrsTexIoResult LoadTrsTex(const std::string& path, BoneTrsTexture& out, uint64_t expectedFingerprint)
    {
        out.Reset();
        TrsTexIoResult r = ReadTrsTexHeader(path, out.Header());
        if (!r.bOk)
        {
            return r;
        }
        r.bOk = false;

        const TrsTexHeader& h = out.Header();
        if (h.NumNodes == 0 || h.Width != h.NumNodes * 3u || h.Height == 0 || h.NumClips == 0)
        {
            r.Error = fmt::format("load: '{}' has inconsistent dimensions", path);
            return r;
        }
        if (expectedFingerprint != 0 && h.Fingerprint != expectedFingerprint)
        {
            r.Error = fmt::format("load: '{}' is stale (file fingerprint {:016x}, source {:016x}); re-run TrsTextureBaker",
                                  path,
                                  h.Fingerprint,
                                  expectedFingerprint);
            return r;
        }

        std::ifstream in(path, std::ios::binary);
        in.seekg(sizeof(TrsTexHeader));
        out.Clips().resize(h.NumClips);
        in.read(reinterpret_cast<char*>(out.Clips().data()), h.NumClips * sizeof(TrsTexClipRecord));
        out.Texels().resize(static_cast<size_t>(h.Width) * h.Height * 4u);
        in.read(reinterpret_cast<char*>(out.Texels().data()), out.Texels().size() * sizeof(uint16_t));
        if (!in.good())
        {
            r.Error = fmt::format("load: '{}' is truncated", path);
            out.Reset();
            return r;
        }

        uint32_t rows = 0;
        for (const TrsTexClipRecord& c : out.Clips())
        {
            if (c.FirstRow != rows || c.FrameCount == 0)
            {
                r.Error = fmt::format("load: '{}' has a corrupt clip table", path);
                out.Reset();
                return r;
            }
            rows += c.FrameCount;
        }
        if (rows != h.Height)
        {
            r.Error = fmt::format("load: '{}' clip rows ({}) do not add up to height ({})", path, rows, h.Height);
            out.Reset();
            return r;
        }

        r.bOk = true;
        return r;
    }
} // namespace RAnimation
