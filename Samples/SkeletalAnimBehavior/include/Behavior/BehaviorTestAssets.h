#pragma once
#include <string>

namespace RAnimation
{
    class BehaviorSystem;

    // Writes the behaviour trees of the test level (behavior_test.level.usda) and the global blackboard schema
    // into Assets/SkeletalAnimBehavior/behavior/ (--write-behavior-assets). The trees are authored in code once
    // so they are valid by construction; afterwards they are ordinary assets the editor opens and edits.
    //   corridor   walk forward until touching another character, then (Cooldown) walk back and stay
    //   bouncer    jog forward; touching a boundary wall turns it away along the wall normal
    //   patrol     FindMarker(route "patrol") -> MoveTo -> next index, forever
    //   watcher    sees the player -> raises the global world.alarm and faces the last seen position
    //   lookout    idle (the four FOV-array NPCs only perceive)
    //   greeter    an interaction request (U) -> face the requester and wave
    //   talker     talk (PlayAction, waits); a talk that ends Cancelled / Rejected leads to the other branch
    bool WriteBehaviorTestAssets(const BehaviorSystem& system, std::string& outError);
} // namespace RAnimation
