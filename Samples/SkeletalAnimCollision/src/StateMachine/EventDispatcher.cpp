#include <StateMachine/EventDispatcher.h>

namespace RAnimation
{
    EventDispatcher::Handle EventDispatcher::Subscribe(EventId id, Listener listener)
    {
        if (!listener)
        {
            return kInvalidHandle;
        }
        const Handle handle = mNextHandle++;
        mListeners[id].push_back({handle, std::move(listener)});
        return handle;
    }

    void EventDispatcher::Unsubscribe(Handle handle)
    {
        for (auto& [id, entries] : mListeners)
        {
            for (size_t i = 0; i < entries.size(); ++i)
            {
                if (entries[i].handle == handle)
                {
                    entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(i));
                    return;
                }
            }
        }
    }

    void EventDispatcher::Dispatch(const Event& event) const
    {
        auto it = mListeners.find(event.id);
        if (it == mListeners.end())
        {
            return;
        }
        // Iterate a copy: a listener may (un)subscribe while being notified.
        const std::vector<Entry> entries = it->second;
        for (const Entry& e : entries)
        {
            e.listener(event);
        }
    }

    void EventDispatcher::Clear()
    {
        mListeners.clear();
    }

    size_t EventDispatcher::ListenerCount(EventId id) const
    {
        auto it = mListeners.find(id);
        return it != mListeners.end() ? it->second.size() : 0;
    }
} // namespace RAnimation
