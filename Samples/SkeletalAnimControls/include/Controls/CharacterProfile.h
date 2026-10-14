#pragma once
#include <map>
#include <string>

#include <AnimGraph/MontagePlayback.h>

// Per-model data of the controllable character: which resident graph to use, the speeds the controller
// moves at, blend times, and the montage table (name -> clip / slot / envelope / priority / notifies).
// The state machine definition is shared between models and refers to montages and graph inputs by
// name only; everything model-specific lives here (<asset dir>/<Stem>.profile.json).

namespace RAnimation
{
    class Model;

    struct ProfileMontage
    {
        std::string slot; // AnimSlot name in the graph ("UpperBody" / "FullBody")
        MontageDesc desc;
    };

    struct CharacterProfile
    {
        std::string path;      // the json this was loaded from
        std::string graphPath; // absolute path of the resident .animgraph.usda

        float walkSpeed = 1.2f;   // m/s
        float jogSpeed = 3.0f;
        float sprintSpeed = 5.0f;
        float crouchSpeed = 0.9f;
        float jumpSpeed = 4.0f;   // vertical take-off velocity, m/s
        float gravity = 9.81f;
        float turnDegPerSec = 120.0f;

        float locoBlendSec = 0.2f;       // CrossfadeSelect blend time written at Attach
        float speedBlendRatePerSec = 6.0f;      // easing of the blend space inputs (m/s per s)
        float directionBlendRateDegPerSec = 540.0f;
        float alphaBlendRatePerSec = 4.0f;       // LayeredBlend alpha easing

        std::string upperBodyMaskRoot; // informational (the graph authors the mask); empty = graph default

        std::map<std::string, ProfileMontage> montages;

        const ProfileMontage* FindMontage(const std::string& name) const
        {
            auto it = montages.find(name);
            return it != montages.end() ? &it->second : nullptr;
        }

        // <dir of the model's asset usda>/<Stem>.profile.json, e.g. ual_mannequin/UAL_Mannequin.profile.json
        static std::string PathForModel(const Model& model);
        static bool Load(const std::string& path, CharacterProfile& out, std::string& outError);
    };
} // namespace RAnimation
