#pragma once

#include <memory>
#include <string>

#include <Model/InstanceSettings.h>
#include <Model/ModelInstance.h>

#include <Editor/Command.h>

namespace RAnimation
{
    // Undoable edit of a single instance's settings (transform, swap-axis, animation clip/speed).
    // Captures before/after snapshots. On apply it copies every field EXCEPT mAnimPlayTimePos from
    // the snapshot onto the instance's current settings, so undo/redo never rewinds live playback.
    class InstanceSettingsCommand final : public ICommand
    {
    public:
        InstanceSettingsCommand(std::string name,
                                std::shared_ptr<ModelInstance> instance,
                                const InstanceSettings& before,
                                const InstanceSettings& after)
            : mName(std::move(name)), mInstance(std::move(instance)), mBefore(before), mAfter(after)
        {
        }

        void Do() override
        {
            Apply(mAfter);
        }

        void Undo() override
        {
            Apply(mBefore);
        }

        const char* Name() const override
        {
            return mName.c_str();
        }

    private:
        void Apply(const InstanceSettings& snapshot)
        {
            if (!mInstance)
            {
                return;
            }

            InstanceSettings settings = mInstance->GetInstanceSettings();
            const float livePlayTime = settings.mAnimPlayTimePos;
            settings = snapshot;
            settings.mAnimPlayTimePos = livePlayTime;
            mInstance->SetInstanceSettings(settings);
        }

        std::string mName;
        std::shared_ptr<ModelInstance> mInstance;
        InstanceSettings mBefore;
        InstanceSettings mAfter;
    };
} // namespace RAnimation
