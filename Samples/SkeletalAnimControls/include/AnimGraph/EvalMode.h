#pragma once

#include <cstdint>

namespace RAnimation
{
    // The three GPU dispatch tiers plus the CPU-only baseline. All tiers run the same hybrid_eval kernel;
    // they differ only in how the CPU issues the dispatches and fills the Work table:
    //   Cpu          - whole graph on the CPU, one upload slot per instance, Dispatch(N,1,1) per model
    //   PerCharacter - A: one Work per instance, Dispatch(1,1,1) issued once per instance
    //   PerNpc       - B: one Work per instance, Dispatch(N,1,1) once per model
    //   Tracks       - C: one Work per (clip, phase bucket) track, Dispatch(T,1,1); crowd draw indexes tracks
    enum class EvalMode : uint8_t
    {
        Cpu = 0,
        PerCharacter,
        PerNpc,
        Tracks,
        Count
    };

    inline const char* EvalModeName(EvalMode mode)
    {
        switch (mode)
        {
        case EvalMode::Cpu:
            return "CPU (baseline)";
        case EvalMode::PerCharacter:
            return "A: per character";
        case EvalMode::PerNpc:
            return "B: per NPC";
        case EvalMode::Tracks:
            return "C: tracks (crowd)";
        default:
            return "?";
        }
    }

    inline bool EvalModeUsesTrsTexture(EvalMode mode)
    {
        return mode != EvalMode::Cpu;
    }
} // namespace RAnimation
