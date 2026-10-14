#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include <StateMachine/StateMachineTypes.h>

namespace RAnimation
{
    // Multicast listeners keyed by event id. Independent of the state tree: SMManager::DispatchEvent
    // notifies the listeners first and then hands the event to the active states (bubbling), so game
    // code can observe events without being a state.
    class EventDispatcher
    {
    public:
        using Listener = std::function<void(const Event&)>;
        using Handle = uint32_t;
        static constexpr Handle kInvalidHandle = 0;

        Handle Subscribe(EventId id, Listener listener);
        void Unsubscribe(Handle handle);
        void Dispatch(const Event& event) const;
        void Clear();
        size_t ListenerCount(EventId id) const;

    private:
        struct Entry
        {
            Handle handle;
            Listener listener;
        };
        std::map<EventId, std::vector<Entry>> mListeners;
        Handle mNextHandle = 1;
    };
} // namespace RAnimation
