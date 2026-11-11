#pragma once

namespace RAnimation
{
    // SkeletalAnimBehavior --selftest, behaviour part (no window, no GPU): composite / decorator semantics,
    // scheduling and wake-ups, blackboard scopes and reset rules, the validator, intent channel ownership,
    // steering, and the tree / blackboard / checkpoint file round trips. Returns the number of failed checks.
    int RunBehaviorSelfTests();
} // namespace RAnimation
