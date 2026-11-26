#include <Agent/PerceptionKeys.h>

namespace RAnimation
{
    const BlackboardSchema& PerceptionSchema()
    {
        static const BlackboardSchema schema = []
        {
            using namespace PerceptionKeys;
            BlackboardSchema s;
            auto add = [&s](const char* name, BbType type, BbValue def, bool wake)
            {
                BlackboardKeyDecl d;
                d.name = name;
                d.type = type;
                d.scope = BbScope::Instance;
                d.defaultValue = std::move(def);
                d.wakeOnChange = wake;
                s.keys.push_back(std::move(d));
            };
            add(kContactEntity, BbType::Entity, EntityId{0}, true);
            add(kContactTime, BbType::Float, 0.0f, false);
            add(kContactCount, BbType::Int, 0, false);
            add(kBoundaryTouching, BbType::Bool, false, true);
            add(kBoundaryTime, BbType::Float, 0.0f, false);
            add(kBoundaryNormal, BbType::Vec3, glm::vec3(0.0f), false);
            add(kPlayerEntity, BbType::Entity, EntityId{0}, false);
            add(kPlayerDistance, BbType::Float, 1e6f, false);
            add(kPlayerInRange, BbType::Bool, false, true);
            add(kPlayerInFov, BbType::Bool, false, false);
            add(kNearestEntity, BbType::Entity, EntityId{0}, false);
            add(kPlayerVisible, BbType::Bool, false, true);
            add(kPlayerLastSeenPos, BbType::Vec3, glm::vec3(0.0f), false);
            add(kPlayerLastSeenTime, BbType::Float, 0.0f, false);
            add(kZone, BbType::String, std::string(), true);
            add(kZoneEnterTime, BbType::Float, 0.0f, false);
            add(kInteractionFrom, BbType::Entity, EntityId{0}, false);
            add(kInteractionTime, BbType::Float, 0.0f, true);
            return s;
        }();
        return schema;
    }
} // namespace RAnimation
