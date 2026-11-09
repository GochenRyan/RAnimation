#pragma once

namespace RAnimation
{
    // Pure-CPU checks of the collision module: collision.json parsing (inline + the shipped UAL profile),
    // CollisionWorld bodies / manifolds / pushes in single-threaded and thread-pool mode, push symmetry across
    // a packed group, body removal. Prints one line per check and returns the number of failures. Part of
    // `SkeletalAnimCollision --selftest`; needs no window or GPU.
    int RunCollisionSelfTests();
} // namespace RAnimation
