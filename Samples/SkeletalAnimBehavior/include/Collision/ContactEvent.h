#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <tuple>
#include <vector>

#include <glm/glm.hpp>

#include <Model/EntityId.h>
#include <Model/LevelStatics.h>

namespace RAnimation
{
    // One merged contact change between two entities (or an entity and a static body), produced by
    // CollisionSystem at the end of frame N and consumed by Perception at the start of frame N + 1 (§2.2).
    struct ContactEvent
    {
        EntityId a = kInvalidEntityId;
        EntityId b = kInvalidEntityId; // kInvalidEntityId: a static body (tag says which kind)
        bool staticBody = false;
        StaticColliderTag tag = StaticColliderTag::Obstacle;
        bool begin = true;                     // false = End
        glm::vec3 normalOnA = glm::vec3(0.0f); // horizontal, pointing away from b (towards a)
    };

    // A contact record as the Jolt callbacks report it (worker threads -> buffer -> main thread). Owners are
    // body slots, or kStaticOwnerBit | static index.
    struct RawContactEvent
    {
        uint32_t owner1 = 0;
        uint32_t owner2 = 0;
        uint32_t sub1 = 0; // sub shape id value (pair identity only)
        uint32_t sub2 = 0;
        bool touching = false;                // depth > 0 (speculative manifolds and OnContactRemoved: false)
        glm::vec3 normal = glm::vec3(0.0f);   // world, from body 1 towards body 2 (touching only)
    };
    inline constexpr uint32_t kStaticOwnerBit = 0x80000000u;

    struct ContactOwnerInfo
    {
        bool valid = false;
        bool isStatic = false;
        EntityId entity = kInvalidEntityId;
        StaticColliderTag tag = StaticColliderTag::Obstacle;
    };

    // D19, §7.3: sub-shape pair -> touching table, summed per owner pair; 0 -> n is Begin, n -> 0 is End.
    // Speculative contacts never begin anything; Persisted is processed too (a pair may start speculative and
    // touch later). Pure data, so the self test can drive it with synthetic records.
    class ContactMerger
    {
    public:
        using Resolve = std::function<ContactOwnerInfo(uint32_t owner)>;

        void Apply(std::span<const RawContactEvent> raw, const Resolve& resolve, std::vector<ContactEvent>& out);
        // A body goes away: End for every pair it is in, right now (Jolt's late OnContactRemoved for it finds
        // nothing left and is ignored).
        void RemoveOwner(uint32_t owner, const Resolve& resolve, std::vector<ContactEvent>& out);
        void Clear();
        size_t TouchingOwnerPairs() const;

    private:
        using SubKey = std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>; // ownerA < ownerB, subA, subB
        struct SubState
        {
            bool touching = false;
            glm::vec3 normalOnA = glm::vec3(0.0f);
        };
        void Emit(uint32_t ownerA, uint32_t ownerB, bool begin, const glm::vec3& normalOnA, const Resolve& resolve, std::vector<ContactEvent>& out);

        std::map<SubKey, SubState> mSubs;
        std::map<std::pair<uint32_t, uint32_t>, int> mPairs;
    };
} // namespace RAnimation
