#pragma once
#include <algorithm>
#include <vector>

namespace RAnimation
{
    // The weight bookkeeping of CrossfadeSelect, kept free of nodes so it can be unit-tested: a list of
    // inputs "in play", each with a weight that ramps towards 1 when it is the active one and towards 0
    // otherwise, by dt / blendTime per step (blendTime <= 0 switches instantly). Inputs whose weight falls
    // below kMinWeight are dropped. Switching back to a fading input resumes from its current weight, so a
    // change of mind mid-fade never pops.
    class CrossfadeTracker
    {
    public:
        struct Layer
        {
            int index = -1;
            float weight = 0.0f;
        };

        static constexpr float kMinWeight = 0.0001f;

        // Makes `index` the active input. `seedFull` puts it in play at weight 1 at once (used when a graph
        // starts, so the first frame does not fade in from nothing).
        void SetActive(int index, bool seedFull = false)
        {
            mActive = index;
            Layer* layer = Find(index);
            if (layer == nullptr)
            {
                mLayers.push_back({index, seedFull ? 1.0f : 0.0f});
            }
            else if (seedFull)
            {
                layer->weight = 1.0f;
            }
        }

        int Active() const { return mActive; }

        // One step: the active layer rises, the others fall, exhausted layers are removed. Returns true
        // when only the active layer remains at full weight (no fade in flight).
        bool Step(float dt, float blendTimeSec)
        {
            if (mActive >= 0 && Find(mActive) == nullptr)
            {
                mLayers.push_back({mActive, 0.0f});
            }
            const float step = blendTimeSec > 0.0f ? std::max(0.0f, dt) / blendTimeSec : 1.0f;
            for (Layer& l : mLayers)
            {
                l.weight = l.index == mActive ? std::min(1.0f, l.weight + step) : std::max(0.0f, l.weight - step);
            }
            mLayers.erase(std::remove_if(mLayers.begin(),
                                         mLayers.end(),
                                         [this](const Layer& l) { return l.index != mActive && l.weight < kMinWeight; }),
                          mLayers.end());
            return mLayers.size() == 1 && mLayers.front().index == mActive && mLayers.front().weight >= 1.0f;
        }

        const std::vector<Layer>& Layers() const { return mLayers; }
        float WeightOf(int index) const
        {
            const Layer* l = Find(index);
            return l != nullptr ? l->weight : 0.0f;
        }
        float TotalWeight() const
        {
            float sum = 0.0f;
            for (const Layer& l : mLayers)
            {
                sum += l.weight;
            }
            return sum;
        }
        void Clear() { mLayers.clear(); }

    private:
        Layer* Find(int index)
        {
            for (Layer& l : mLayers)
            {
                if (l.index == index)
                {
                    return &l;
                }
            }
            return nullptr;
        }
        const Layer* Find(int index) const { return const_cast<CrossfadeTracker*>(this)->Find(index); }

        std::vector<Layer> mLayers;
        int mActive = -1;
    };
} // namespace RAnimation
