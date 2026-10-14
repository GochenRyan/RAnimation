#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace RAnimation
{
    struct MontageNotify
    {
        float timeSec = 0.0f;
        std::string name;
    };

    // "A clip with a script": what a slot plays once (or looping) on request. clipName is resolved against
    // the model when the montage starts; durationSec is filled in at the same time.
    struct MontageDesc
    {
        std::string name;        // e.g. "punch" (profile key)
        std::string clipName;    // full or short clip name
        int clipIndex = -1;      // resolved at Play
        float durationSec = 0.0f; // resolved at Play
        float playRate = 1.0f;
        float blendInSec = 0.1f;
        float blendOutSec = 0.2f;
        bool loop = false;
        int priority = 0;        // a new request only replaces a playing montage of lower or equal priority
        bool lockMovement = false;
        std::vector<MontageNotify> notifies;
    };

    // Playback state of one montage in a slot: time, and a weight envelope (blend in -> hold -> blend out).
    // A non-looping montage starts its blend-out so that it reaches 0 exactly at the clip end; Stop()
    // starts it early. Free of nodes so it can be unit-tested.
    class MontagePlayback
    {
    public:
        enum class Phase
        {
            In,
            Hold,
            Out,
            Finished
        };

        explicit MontagePlayback(MontageDesc desc) : mDesc(std::move(desc))
        {
            if (mDesc.blendInSec <= 0.0f)
            {
                mWeight = 1.0f;
                mPhase = Phase::Hold;
            }
        }

        // Advances time and the envelope; appends the names of the notifies crossed in this step.
        void Advance(float dt, std::vector<std::string>* crossed)
        {
            if (mPhase == Phase::Finished)
            {
                return;
            }
            const float prev = mTime;
            bool outStartedNow = false;
            mTime += std::max(0.0f, dt) * mDesc.playRate;
            const float duration = mDesc.durationSec;
            if (duration > 0.0f)
            {
                if (mDesc.loop)
                {
                    if (mTime >= duration)
                    {
                        Collect(prev, duration, crossed);
                        mTime = std::fmod(mTime, duration);
                        Collect(-1.0f, mTime, crossed); // notifies at t = 0 fire again on wrap
                    }
                    else
                    {
                        Collect(prev, mTime, crossed);
                    }
                }
                else
                {
                    const float clamped = std::min(mTime, duration);
                    Collect(prev, clamped, crossed);
                    mTime = clamped;
                    // natural end: start blending out so weight hits 0 at the clip end. The part of this
                    // step already past the start point counts towards the blend-out.
                    if (mPhase != Phase::Out && mTime >= duration - mDesc.blendOutSec)
                    {
                        BeginOut();
                        const float rate = mDesc.playRate > 0.0f ? mDesc.playRate : 1.0f;
                        mOutElapsed = std::max(0.0f, (mTime - (duration - mDesc.blendOutSec)) / rate);
                        outStartedNow = true;
                    }
                }
            }
            else
            {
                Collect(prev, mTime, crossed);
            }

            const float step = std::max(0.0f, dt);
            switch (mPhase)
            {
                case Phase::In:
                    mWeight = mDesc.blendInSec > 0.0f ? std::min(1.0f, mWeight + step / mDesc.blendInSec) : 1.0f;
                    if (mWeight >= 1.0f)
                    {
                        mPhase = Phase::Hold;
                    }
                    break;
                case Phase::Hold:
                    mWeight = 1.0f;
                    break;
                case Phase::Out:
                    if (!outStartedNow)
                    {
                        mOutElapsed += step;
                    }
                    mWeight = mDesc.blendOutSec > 0.0f ? mOutStartWeight * std::max(0.0f, 1.0f - mOutElapsed / mDesc.blendOutSec) : 0.0f;
                    if (mWeight <= 0.0f)
                    {
                        mWeight = 0.0f;
                        mPhase = Phase::Finished;
                    }
                    break;
                case Phase::Finished:
                    break;
            }
            if (!mDesc.loop && duration > 0.0f && mTime >= duration && mDesc.blendOutSec <= 0.0f)
            {
                mWeight = 0.0f;
                mPhase = Phase::Finished;
            }
        }

        // Starts the blend-out now (another montage took the slot, or the owner stopped it).
        void Stop(float blendOutSec)
        {
            if (mPhase == Phase::Finished)
            {
                return;
            }
            mDesc.blendOutSec = std::max(0.0f, blendOutSec);
            BeginOut();
            if (mDesc.blendOutSec <= 0.0f)
            {
                mWeight = 0.0f;
                mPhase = Phase::Finished;
            }
        }

        const MontageDesc& Desc() const { return mDesc; }
        float Time() const { return mTime; }
        float Weight() const { return mWeight; }
        Phase CurrentPhase() const { return mPhase; }
        bool Finished() const { return mPhase == Phase::Finished; }
        bool BlendingOut() const { return mPhase == Phase::Out; }

    private:
        void BeginOut()
        {
            if (mPhase == Phase::Out)
            {
                return;
            }
            mPhase = Phase::Out;
            mOutStartWeight = mWeight;
            mOutElapsed = 0.0f;
        }

        // notifies with prev < t <= now
        void Collect(float prev, float now, std::vector<std::string>* crossed) const
        {
            if (crossed == nullptr)
            {
                return;
            }
            for (const MontageNotify& n : mDesc.notifies)
            {
                if (n.timeSec > prev && n.timeSec <= now)
                {
                    crossed->push_back(n.name);
                }
            }
        }

        MontageDesc mDesc;
        float mTime = 0.0f;
        float mWeight = 0.0f;
        Phase mPhase = Phase::In;
        float mOutStartWeight = 1.0f;
        float mOutElapsed = 0.0f;
    };
} // namespace RAnimation
