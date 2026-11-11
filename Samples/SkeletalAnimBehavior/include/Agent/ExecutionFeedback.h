#pragma once
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include <Agent/CharacterSignals.h>

namespace RAnimation
{
    using RequestId = uint32_t; // per agent, monotonically increasing, 0 = none

    enum class RequestStatus : uint8_t
    {
        Pending,   // queued this frame, the controller has not processed it yet
        Accepted,  // the busy state took it and the slot plays the montage
        Rejected,  // unknown montage, dead, or outranked by what is playing
        Completed, // the slot finished it
        Cancelled  // CancelAction, interrupted by a higher priority montage, or unknown id (after a load)
    };

    inline const char* RequestStatusName(RequestStatus s)
    {
        switch (s)
        {
            case RequestStatus::Pending: return "Pending";
            case RequestStatus::Accepted: return "Accepted";
            case RequestStatus::Rejected: return "Rejected";
            case RequestStatus::Completed: return "Completed";
            case RequestStatus::Cancelled: return "Cancelled";
        }
        return "?";
    }

    inline bool IsFinished(RequestStatus s)
    {
        return s == RequestStatus::Rejected || s == RequestStatus::Completed || s == RequestStatus::Cancelled;
    }

    // Execution layer -> decision layer, rebuilt every frame after the controller update.
    struct ExecutionFeedback
    {
        MotionState motion;
        bool armed = false;
        bool dead = false;
        bool arrived = false;          // MoveTo and horizontal distance <= arriveRadius
        float distanceToTarget = 0.0f; // MoveTo / FaceTo target (horizontal)
        float facingErrorDeg = 0.0f;   // desired yaw - current yaw for MoveTo / FaceTo
        bool blocked = false;          // MoveTo and < blockedEpsilon moved horizontally during blockedSeconds
        std::vector<std::pair<RequestId, RequestStatus>> statusChanges; // changed this frame (wakes the agent)
        std::map<RequestId, RequestStatus> requests;                    // the controller's request table

        // A request the table does not know is Cancelled: that is how "unfinished requests are reported as
        // cancelled after a load" (D15) falls out without extra code.
        RequestStatus Query(RequestId id) const
        {
            const auto it = requests.find(id);
            return it != requests.end() ? it->second : RequestStatus::Cancelled;
        }
    };
} // namespace RAnimation
