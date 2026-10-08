// Model: CPU-side TRS texture loading and node-order pose helpers (split out of Model.cpp).

#include <fmt/base.h>
#include <fmt/color.h>
#include <fmt/format.h>
#include <glm/gtc/matrix_transform.hpp>

#include <Model/Model.h>
#include <Model/TrsSource.h>
#include <Model/UsdModelLoader.h>
#include <TrsTexture/TrsTextureFile.h>

using namespace RAnimation;

void Model::loadTrsTextureCpu(const UsdLoadedModel& loaded, const std::string& assetPath)
{
    mTrsTexReady = false;
    mTrsTexCpu.Reset();
    mTrsTexPath = TrsTexturePathForAsset(assetPath);

    if (mAnimClips.empty())
    {
        mTrsTexStatus = "static model (no clips)";
        return;
    }

    TrsTexHeader header;
    TrsTexIoResult result = ReadTrsTexHeader(mTrsTexPath, header);
    if (!result.bOk)
    {
        mTrsTexStatus = result.Error;
        fmt::print(stderr, fg(fmt::color::yellow), "{} warning: TRS texture unavailable: {}\n", __FUNCTION__, result.Error);
        return;
    }

    // The fingerprint covers the source data plus the header fields that change texel contents; those
    // header fields come from the file so a --rate override is not mistaken for a stale bake.
    const TrsSourceView view = BuildTrsSourceView(loaded);
    const uint64_t expected = ComputeTrsSourceFingerprint(view, header.Version, header.Layout, header.SampleRate);
    result = LoadTrsTex(mTrsTexPath, mTrsTexCpu, expected);
    if (!result.bOk)
    {
        mTrsTexStatus = result.Error;
        fmt::print(stderr, fg(fmt::color::yellow), "{} warning: TRS texture rejected: {}\n", __FUNCTION__, result.Error);
        mTrsTexCpu.Reset();
        return;
    }

    if (mTrsTexCpu.NumNodes() != mNodeList.size() || mTrsTexCpu.NumClips() != mAnimClips.size())
    {
        mTrsTexStatus = fmt::format("TRS texture shape mismatch (nodes {} vs {}, clips {} vs {})",
                                    mTrsTexCpu.NumNodes(),
                                    mNodeList.size(),
                                    mTrsTexCpu.NumClips(),
                                    mAnimClips.size());
        fmt::print(stderr, fg(fmt::color::yellow), "{} warning: {}\n", __FUNCTION__, mTrsTexStatus);
        mTrsTexCpu.Reset();
        return;
    }

    mTrsTexReady = true;
    mTrsTexStatus = fmt::format("ok: {}x{} RGBA16F, {} clips, {:.0f} Hz, {} KB",
                                mTrsTexCpu.Width(),
                                mTrsTexCpu.Height(),
                                mTrsTexCpu.NumClips(),
                                mTrsTexCpu.Header().SampleRate,
                                mTrsTexCpu.TexelBytes() / 1024);
    fmt::print("{}: TRS texture {}\n", __FUNCTION__, mTrsTexStatus);
}

void Model::ComputeNodeGlobals(const Pose& pose, const glm::mat4& instanceLocal, std::vector<glm::mat4>& out) const
{
    const size_t nodeCount = mNodeList.size();
    out.resize(nodeCount);
    for (size_t i = 0; i < nodeCount; ++i)
    {
        const Transform& t = i < pose.count ? pose.bones[i] : mBindPose[i];
        const glm::mat4 local = glm::translate(glm::mat4(1.0f), t.T) * glm::mat4_cast(glm::normalize(t.R)) *
                                glm::scale(glm::mat4(1.0f), t.S);
        const int32_t parent = i < mNodeParentIndex.size() ? mNodeParentIndex[i] : -1;
        // Same composition as Node::UpdateTRSMatrix: the instance transform is injected at the root.
        out[i] = parent >= 0 ? out[static_cast<size_t>(parent)] * local : instanceLocal * local;
    }
}

int32_t Model::FindNodeIndex(const std::string& name) const
{
    const auto it = mNodeIndexByName.find(name);
    return it != mNodeIndexByName.end() ? it->second : -1;
}
