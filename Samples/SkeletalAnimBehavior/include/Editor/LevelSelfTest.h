#pragma once

namespace RAnimation
{
    // Level IO checks for SkeletalAnimBehavior --selftest (no window, no GPU): static layers authored the
    // way Maya / Blender export them (root unit / axis conversion), level customData (entity ids, agent
    // config, subLayers) and SceneEditor entity id allocation. Returns the number of failed checks.
    int RunLevelSelfTests();
} // namespace RAnimation
