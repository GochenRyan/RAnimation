#pragma once

namespace RAnimation
{
    // Pure-CPU checks of the state-machine module (hierarchy, parallel regions, events, priorities,
    // validation, USD round trips, the character definition). Prints one line per check and returns the
    // number of failures. Run with `SkeletalAnimControls --selftest`; needs no window or GPU.
    int RunStateMachineSelfTests();
} // namespace RAnimation
