#include <Collision/ContactEvent.h>

#include <utility>

namespace RAnimation
{
    namespace
    {
        glm::vec3 Horizontal(glm::vec3 v)
        {
            v.y = 0.0f;
            const float len = glm::length(v);
            return len > 1e-6f ? v / len : glm::vec3(0.0f);
        }
    } // namespace

    void ContactMerger::Emit(uint32_t ownerA, uint32_t ownerB, bool begin, const glm::vec3& normalOnA, const Resolve& resolve,
                             std::vector<ContactEvent>& out)
    {
        const ContactOwnerInfo a = resolve(ownerA);
        const ContactOwnerInfo b = resolve(ownerB);
        if (!a.valid || !b.valid || (a.isStatic && b.isStatic))
        {
            return;
        }
        // one event per character side; a static body has no side of its own
        if (!a.isStatic)
        {
            ContactEvent e;
            e.a = a.entity;
            e.b = b.isStatic ? kInvalidEntityId : b.entity;
            e.staticBody = b.isStatic;
            e.tag = b.tag;
            e.begin = begin;
            e.normalOnA = normalOnA;
            out.push_back(e);
        }
        if (!b.isStatic)
        {
            ContactEvent e;
            e.a = b.entity;
            e.b = a.isStatic ? kInvalidEntityId : a.entity;
            e.staticBody = a.isStatic;
            e.tag = a.tag;
            e.begin = begin;
            e.normalOnA = -normalOnA;
            out.push_back(e);
        }
    }

    void ContactMerger::Apply(std::span<const RawContactEvent> raw, const Resolve& resolve, std::vector<ContactEvent>& out)
    {
        for (const RawContactEvent& r : raw)
        {
            uint32_t oa = r.owner1, ob = r.owner2, sa = r.sub1, sb = r.sub2;
            // normal points from body 1 to body 2: body 1 is pushed along -normal
            glm::vec3 normalOnA = Horizontal(-r.normal);
            if (oa > ob)
            {
                std::swap(oa, ob);
                std::swap(sa, sb);
                normalOnA = -normalOnA;
            }
            if (oa == ob)
            {
                continue;
            }
            const SubKey key{oa, ob, sa, sb};
            auto it = mSubs.find(key);
            const bool was = it != mSubs.end() && it->second.touching;
            if (r.touching)
            {
                SubState& s = mSubs[key];
                s.touching = true;
                s.normalOnA = normalOnA;
            }
            else if (it != mSubs.end())
            {
                mSubs.erase(it); // speculative or removed: this sub pair no longer touches
            }
            if (was == r.touching)
            {
                continue;
            }
            int& count = mPairs[{oa, ob}];
            if (r.touching)
            {
                if (count++ == 0)
                {
                    Emit(oa, ob, true, normalOnA, resolve, out);
                }
            }
            else if (--count <= 0)
            {
                mPairs.erase({oa, ob});
                Emit(oa, ob, false, normalOnA, resolve, out);
            }
        }
    }

    void ContactMerger::RemoveOwner(uint32_t owner, const Resolve& resolve, std::vector<ContactEvent>& out)
    {
        for (auto it = mPairs.begin(); it != mPairs.end();)
        {
            if (it->first.first == owner || it->first.second == owner)
            {
                Emit(it->first.first, it->first.second, false, glm::vec3(0.0f), resolve, out);
                it = mPairs.erase(it);
            }
            else
            {
                ++it;
            }
        }
        for (auto it = mSubs.begin(); it != mSubs.end();)
        {
            if (std::get<0>(it->first) == owner || std::get<1>(it->first) == owner)
                it = mSubs.erase(it);
            else
                ++it;
        }
    }

    void ContactMerger::Clear()
    {
        mSubs.clear();
        mPairs.clear();
    }

    size_t ContactMerger::TouchingOwnerPairs() const
    {
        return mPairs.size();
    }
} // namespace RAnimation
