#pragma once
#include <cstdint>
#include <vector>

namespace RAnimation
{
    // Per-node blend mask for LayeredBlend, in Model::GetNodeList() order: 1 inside the subtree rooted at
    // rootIndex, 0 elsewhere. With blendDepth k > 0 the first k generations under the root ramp in,
    // (d + 1) / (k + 1) for a node at depth d < k, so the layer fades in along the spine instead of
    // switching on one joint. rootIndex < 0 gives an all-zero mask (the layer never shows).
    inline std::vector<float> BuildBoneMask(const std::vector<int32_t>& parentIndices, int rootIndex, int blendDepth)
    {
        const size_t count = parentIndices.size();
        std::vector<float> mask(count, 0.0f);
        if (rootIndex < 0 || static_cast<size_t>(rootIndex) >= count)
        {
            return mask;
        }
        for (size_t n = 0; n < count; ++n)
        {
            int depth = 0;
            int cur = static_cast<int>(n);
            int guard = 0;
            while (cur >= 0 && cur != rootIndex && guard++ < static_cast<int>(count))
            {
                cur = parentIndices[static_cast<size_t>(cur)];
                ++depth;
            }
            if (cur != rootIndex)
            {
                continue; // not under the root
            }
            mask[n] = (blendDepth > 0 && depth < blendDepth)
                              ? static_cast<float>(depth + 1) / static_cast<float>(blendDepth + 1)
                              : 1.0f;
        }
        return mask;
    }
} // namespace RAnimation
