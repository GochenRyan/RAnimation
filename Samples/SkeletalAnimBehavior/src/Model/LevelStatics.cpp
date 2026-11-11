#include <Model/LevelStatics.h>

#include <algorithm>
#include <cmath>

using namespace RAnimation;

void StaticBox::Corners(glm::vec3 (&out)[8]) const
{
    const glm::mat3 r = glm::mat3_cast(rotation);
    for (int i = 0; i < 8; ++i)
    {
        const glm::vec3 local((i & 1) ? halfExtents.x : -halfExtents.x,
                              (i & 2) ? halfExtents.y : -halfExtents.y,
                              (i & 4) ? halfExtents.z : -halfExtents.z);
        out[i] = center + r * local;
    }
}

bool StaticBox::Contains(const glm::vec3& point) const
{
    const glm::vec3 local = glm::conjugate(rotation) * (point - center);
    return std::abs(local.x) <= halfExtents.x && std::abs(local.y) <= halfExtents.y &&
           std::abs(local.z) <= halfExtents.z;
}

const StaticZone* LevelStatics::ZoneAt(const glm::vec3& point) const
{
    const StaticZone* best = nullptr;
    for (const StaticZone& zone : zones)
    {
        if (zone.box.Contains(point) && (best == nullptr || zone.box.Volume() < best->box.Volume()))
        {
            best = &zone;
        }
    }
    return best;
}

std::vector<const StaticMarker*> LevelStatics::Route(const std::string& route) const
{
    std::vector<const StaticMarker*> points;
    for (const StaticMarker& marker : markers)
    {
        if (marker.kind == StaticMarkerKind::Waypoint && marker.route == route)
        {
            points.push_back(&marker);
        }
    }
    std::stable_sort(points.begin(), points.end(), [](const StaticMarker* a, const StaticMarker* b) { return a->index < b->index; });
    return points;
}

namespace
{
    void boxLines(const StaticBox& box, const glm::vec3& color, DebugLineList& out)
    {
        glm::vec3 c[8];
        box.Corners(c);
        static constexpr int e[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto& edge : e)
        {
            out.Line(c[edge[0]], c[edge[1]], color);
        }
    }

    void cross(const glm::vec3& p, float size, const glm::vec3& color, DebugLineList& out)
    {
        out.Line(p - glm::vec3(size, 0.0f, 0.0f), p + glm::vec3(size, 0.0f, 0.0f), color);
        out.Line(p - glm::vec3(0.0f, 0.0f, size), p + glm::vec3(0.0f, 0.0f, size), color);
        out.Line(p, p + glm::vec3(0.0f, 2.0f * size, 0.0f), color);
    }
} // namespace

void RAnimation::AppendLevelStaticsLines(const LevelStatics& statics, DebugLineList& out)
{
    const glm::vec3 white(1.0f);
    const glm::vec3 orange(1.0f, 0.55f, 0.1f);
    const glm::vec3 cyan(0.2f, 0.9f, 1.0f);
    const glm::vec3 yellow(1.0f, 0.9f, 0.2f);
    const glm::vec3 green(0.3f, 1.0f, 0.3f);

    for (const StaticCollider& collider : statics.colliders)
    {
        boxLines(collider.box, collider.tag == StaticColliderTag::Boundary ? orange : white, out);
    }
    for (const StaticZone& zone : statics.zones)
    {
        boxLines(zone.box, cyan, out);
    }

    std::vector<std::string> routes;
    for (const StaticMarker& marker : statics.markers)
    {
        if (marker.kind == StaticMarkerKind::Spawn)
        {
            cross(marker.position, 0.3f, green, out);
            out.Line(marker.position, marker.position + marker.forward * 0.8f, green);
        }
        else
        {
            cross(marker.position, 0.2f, yellow, out);
            if (marker.radius > 0.0f)
            {
                constexpr int kSegments = 24;
                for (int i = 0; i < kSegments; ++i)
                {
                    const float a0 = 6.2831853f * static_cast<float>(i) / kSegments;
                    const float a1 = 6.2831853f * static_cast<float>(i + 1) / kSegments;
                    out.Line(marker.position + marker.radius * glm::vec3(std::cos(a0), 0.0f, std::sin(a0)),
                             marker.position + marker.radius * glm::vec3(std::cos(a1), 0.0f, std::sin(a1)),
                             yellow);
                }
            }
            if (!marker.route.empty() && std::find(routes.begin(), routes.end(), marker.route) == routes.end())
            {
                routes.push_back(marker.route);
            }
        }
    }
    for (const std::string& route : routes)
    {
        const std::vector<const StaticMarker*> points = statics.Route(route);
        for (size_t i = 0; i + 1 < points.size(); ++i)
        {
            out.Line(points[i]->position, points[i + 1]->position, yellow);
        }
        if (points.size() > 2)
        {
            out.Line(points.back()->position, points.front()->position, yellow * 0.5f);
        }
    }
}
