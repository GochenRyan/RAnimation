#include <Editor/LevelSelfTest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

#include <fmt/base.h>
#include <fmt/format.h>
#include <glm/gtc/quaternion.hpp>

#include <Editor/SceneEditor.h>
#include <Model/LevelStatics.h>
#include <Model/UsdSceneExporter.h>

namespace RAnimation
{
    namespace
    {
        int gChecks = 0;
        int gFails = 0;

        void Check(bool condition, const std::string& what)
        {
            ++gChecks;
            if (!condition)
            {
                ++gFails;
            }
            fmt::print("  [{}] {}\n", condition ? " ok " : "FAIL", what);
        }

        bool Near(const glm::vec3& a, const glm::vec3& b, float tol = 1e-3f)
        {
            return glm::length(a - b) <= tol;
        }

        std::string Str(const glm::vec3& v)
        {
            return fmt::format("({:.3f}, {:.3f}, {:.3f})", v.x, v.y, v.z);
        }

        void WriteFile(const std::filesystem::path& path, const std::string& text)
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << text;
        }

        const StaticCollider* FindCollider(const LevelStatics& s, const std::string& name)
        {
            for (const StaticCollider& c : s.colliders)
            {
                if (c.name == name)
                {
                    return &c;
                }
            }
            return nullptr;
        }

        const StaticMarker* FindMarker(const LevelStatics& s, const std::string& name)
        {
            for (const StaticMarker& m : s.markers)
            {
                if (m.name == name)
                {
                    return &m;
                }
            }
            return nullptr;
        }

        // What `mayaUSDExport ... unit="m"` writes (verified 2026-11-10 with Maya 2026 / mayaUSD 0.32): the
        // values stay in centimetres and the conversion is a 0.01 scale on the root prim.
        const char* kMayaStatic = R"(#usda 1.0
(
    defaultPrim = "Static"
    metersPerUnit = 1
    upAxis = "Y"
)

def Xform "Static" (
    kind = "component"
)
{
    float3 xformOp:scale = (0.01, 0.01, 0.01)
    uniform token[] xformOpOrder = ["xformOp:scale"]

    def Mesh "Ground"
    {
        float3[] extent = [(-1000, 0, -1000), (1000, 0, 1000)]
    }

    def Mesh "COL_Pillar"
    {
        float3[] extent = [(-25, -50, -25), (25, 50, 25)]
        string ranim:collider = "box"
        string ranim:collisionTag = "obstacle"
        double3 xformOp:translate = (300, 50, 200)
        float3 xformOp:rotateXYZ = (0, 30, 0)
        uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateXYZ"]
    }

    def Mesh "BND_North"
    {
        float3[] extent = [(-1000, -100, -10), (1000, 100, 10)]
        string ranim:collider = "box"
        double3 xformOp:translate = (0, 100, -1000)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }

    def Mesh "COL_Untagged"
    {
        float3[] extent = [(-10, -10, -10), (10, 10, 10)]
    }

    def Mesh "ZONE_Plaza"
    {
        float3[] extent = [(-500, -100, -500), (500, 100, 500)]
        string ranim:marker = "zone"
    }

    def Mesh "ZONE_Fountain"
    {
        float3[] extent = [(-100, -100, -100), (100, 100, 100)]
        string ranim:marker = "zone"
        double3 xformOp:translate = (100, 0, 100)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }

    def Xform "WP_patrol_1"
    {
        string ranim:marker = "waypoint"
        float ranim:radius = 0.75
        double3 xformOp:translate = (100, 0, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }

    def Xform "WP_patrol_0"
    {
        string ranim:marker = "waypoint"
        double3 xformOp:translate = (0, 0, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }

    def Xform "WP_patrol_2"
    {
        string ranim:marker = "waypoint"
        double3 xformOp:translate = (100, 0, 100)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }

    def Xform "SPAWN_player"
    {
        string ranim:marker = "spawn"
        double3 xformOp:translate = (-400, 0, 150)
        float3 xformOp:rotateXYZ = (0, 90, 0)
        uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateXYZ"]
    }
}
)";

        // Maya export without unit="m": centimetres declared through stage metadata instead.
        const char* kMayaCentimetreStatic = R"(#usda 1.0
(
    defaultPrim = "Static"
    metersPerUnit = 0.01
    upAxis = "Y"
)

def Xform "Static"
{
    def Mesh "COL_Pillar"
    {
        float3[] extent = [(-25, -50, -25), (25, 50, 25)]
        string ranim:collider = "box"
        double3 xformOp:translate = (300, 50, 200)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }
}
)";

        // What Blender's USD exporter writes with root_prim_path="/Static", up Y: a -90 deg X rotation on the
        // root prim, children keep their Z-up coordinates (pillar at Blender (3, -2, 0.5), 1 m tall along Z).
        const char* kBlenderStatic = R"(#usda 1.0
(
    defaultPrim = "Static"
    metersPerUnit = 1
    upAxis = "Y"
)

def Xform "Static"
{
    float3 xformOp:rotateXYZ = (-90, 0, 0)
    uniform token[] xformOpOrder = ["xformOp:rotateXYZ"]

    def Mesh "COL_Pillar"
    {
        float3[] extent = [(-0.25, -0.25, -0.5), (0.25, 0.25, 0.5)]
        custom string ranim:collider = "box"
        double3 xformOp:translate = (3, -2, 0.5)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }
}
)";

        const char* kAsset = R"(#usda 1.0
(
    defaultPrim = "Asset"
)

def Xform "Asset"
{
}
)";

        // A level as ExportSceneToUsd writes it, plus a legacy instance without an id and a duplicate id.
        const char* kLevel = R"(#usda 1.0
(
    defaultPrim = "Scene"
    metersPerUnit = 1
    subLayers = [
        @maya.static.usda@
    ]
    upAxis = "Y"
)

def Xform "Scene"
{
    def Xform "Instance_0" (
        customData = {
            dictionary ranim = {
                dictionary agent = {
                    float fovHalfDeg = 45
                    bool possessed = 1
                    float proximityRadius = 6
                    float sightRange = 20
                    float tickHz = 5
                    string tree = "behavior/guard.behaviortree.usda"
                }
                int animClipNr = 1
                uint64 entityId = 17
            }
        }
        prepend references = @asset.usda@
    )
    {
        double3 xformOp:translate = (1, 0, 2)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }

    def Xform "Instance_1" (
        customData = {
            dictionary ranim = {
                int animClipNr = 2
            }
        }
        prepend references = @asset.usda@
    )
    {
    }

    def Xform "Instance_2" (
        customData = {
            dictionary ranim = {
                dictionary agent = {
                    bool possessed = 1
                    string tree = "behavior/guard.behaviortree.usda"
                }
                uint64 entityId = 17
            }
        }
        prepend references = @asset.usda@
    )
    {
    }
}
)";

        void TestMayaStatic(const std::filesystem::path& dir)
        {
            fmt::print("\n-- static layer: Maya export (unit=\"m\" -> root scale 0.01) --\n");
            LevelStatics s;
            std::string error;
            Check(LoadLevelStatics((dir / "maya.static.usda").generic_string(), s, error), "LoadLevelStatics opens the layer");

            Check(s.colliders.size() == 2, fmt::format("2 colliders (COL_Pillar, BND_North), got {}", s.colliders.size()));
            if (const StaticCollider* pillar = FindCollider(s, "COL_Pillar"))
            {
                Check(Near(pillar->box.center, glm::vec3(3.0f, 0.5f, 2.0f)), "pillar centre (300, 50, 200) cm -> (3, 0.5, 2) m, got " + Str(pillar->box.center));
                Check(Near(pillar->box.halfExtents, glm::vec3(0.25f, 0.5f, 0.25f)), "pillar half extents in metres (root scale applied), got " + Str(pillar->box.halfExtents));
                const glm::vec3 xAxis = pillar->box.rotation * glm::vec3(1.0f, 0.0f, 0.0f);
                Check(Near(xAxis, glm::vec3(std::cos(glm::radians(30.0f)), 0.0f, -std::sin(glm::radians(30.0f)))), "pillar rotation = rotateY 30 deg, x axis " + Str(xAxis));
                Check(pillar->tag == StaticColliderTag::Obstacle, "pillar tag obstacle");
                Check(pillar->box.Contains(glm::vec3(3.0f, 0.95f, 2.0f)) && !pillar->box.Contains(glm::vec3(3.0f, 1.05f, 2.0f)), "pillar top at y = 1 m");
            }
            else
            {
                Check(false, "COL_Pillar found");
            }
            const StaticCollider* wall = FindCollider(s, "BND_North");
            Check(wall != nullptr && wall->tag == StaticColliderTag::Boundary, "BND_ prefix without ranim:collisionTag -> boundary");
            Check(FindCollider(s, "COL_Untagged") == nullptr, "COL_ without ranim:collider is not a collider");
            bool warned = false;
            for (const std::string& w : s.warnings)
            {
                warned = warned || w.find("COL_Untagged") != std::string::npos;
            }
            Check(warned, "untagged COL_ prim produces a warning");

            Check(s.zones.size() == 2, "2 zones");
            const StaticZone* inner = s.ZoneAt(glm::vec3(1.0f, 0.5f, 1.0f));
            Check(inner != nullptr && inner->name == "Fountain", "nested zones: the smaller one wins (Fountain), ZONE_ prefix stripped");
            const StaticZone* outer = s.ZoneAt(glm::vec3(4.0f, 0.5f, -4.0f));
            Check(outer != nullptr && outer->name == "Plaza", "outside the inner zone: Plaza");
            Check(s.ZoneAt(glm::vec3(20.0f, 0.0f, 0.0f)) == nullptr, "outside every zone: none");

            const std::vector<const StaticMarker*> route = s.Route("patrol");
            Check(route.size() == 3 && route[0]->index == 0 && route[1]->index == 1 && route[2]->index == 2, "route 'patrol' ordered by the WP_<route>_<index> suffix");
            if (route.size() == 3)
            {
                Check(Near(route[1]->position, glm::vec3(1.0f, 0.0f, 0.0f)) && Near(route[2]->position, glm::vec3(1.0f, 0.0f, 1.0f)), "waypoint positions in metres");
                Check(std::fabs(route[1]->radius - 0.75f) < 1e-5f, "ranim:radius is metres and NOT scaled by the root 0.01");
            }
            const StaticMarker* spawn = FindMarker(s, "SPAWN_player");
            Check(spawn != nullptr && spawn->kind == StaticMarkerKind::Spawn && Near(spawn->position, glm::vec3(-4.0f, 0.0f, 1.5f)), "spawn position");
            Check(spawn != nullptr && Near(spawn->forward, glm::vec3(-1.0f, 0.0f, 0.0f)), "spawn forward = local -Z after rotateY 90 -> -X");

            DebugLineList lines;
            AppendLevelStaticsLines(s, lines);
            Check(lines.LineCount() >= 4 * 12, fmt::format("debug wireframes generated ({} lines)", lines.LineCount()));
        }

        void TestOtherExporters(const std::filesystem::path& dir)
        {
            fmt::print("\n-- static layer: Maya without unit (metersPerUnit 0.01), Blender (root Rx(-90)) --\n");
            {
                LevelStatics s;
                std::string error;
                LoadLevelStatics((dir / "maya_cm.static.usda").generic_string(), s, error);
                const StaticCollider* pillar = FindCollider(s, "COL_Pillar");
                Check(pillar != nullptr && Near(pillar->box.center, glm::vec3(3.0f, 0.5f, 2.0f)) && Near(pillar->box.halfExtents, glm::vec3(0.25f, 0.5f, 0.25f)),
                      "metersPerUnit 0.01 stage: same pillar in metres");
            }
            {
                LevelStatics s;
                std::string error;
                LoadLevelStatics((dir / "blender.static.usda").generic_string(), s, error);
                const StaticCollider* pillar = FindCollider(s, "COL_Pillar");
                Check(pillar != nullptr && Near(pillar->box.center, glm::vec3(3.0f, 0.5f, 2.0f)), "Blender (3, -2, 0.5) Z-up -> (3, 0.5, 2) Y-up" + (pillar ? ", got " + Str(pillar->box.center) : std::string()));
                Check(pillar != nullptr && pillar->box.Contains(glm::vec3(3.0f, 0.95f, 2.0f)) && !pillar->box.Contains(glm::vec3(3.0f, 1.05f, 2.0f)) &&
                              !pillar->box.Contains(glm::vec3(3.0f, 0.5f, 2.3f)),
                      "Blender pillar: 1 m tall along world Y, 0.5 m deep");
            }
        }

        void TestLevelImport(const std::filesystem::path& dir, ImportedScene& scene)
        {
            fmt::print("\n-- level customData: entity ids, agent config, subLayers --\n");
            Check(ImportSceneFromUsd((dir / "test.level.usda").generic_string(), scene), "ImportSceneFromUsd");
            Check(scene.instances.size() == 3, fmt::format("3 instances (referenced asset prims pruned), got {}", scene.instances.size()));
            if (scene.instances.size() == 3)
            {
                const ImportedSceneInstance& a = scene.instances[0];
                Check(a.entityId == 17, "uint64 entityId read back");
                Check(a.agent.has_value() && a.agent->tree == "behavior/guard.behaviortree.usda" && a.agent->tickHz == 5.0f && a.agent->fovHalfDeg == 45.0f &&
                              a.agent->proximityRadius == 6.0f && a.agent->possessed,
                      "agent dictionary read back");
                Check(a.settings.mAnimClipNr == 1 && Near(a.settings.mWorldPosition, glm::vec3(1.0f, 0.0f, 2.0f)), "clip and transform still read");
                Check(scene.instances[1].entityId == kInvalidEntityId && !scene.instances[1].agent, "legacy instance: no id, no agent");
                Check(scene.instances[2].entityId == 17, "duplicate id read as authored (resolved by SceneEditor)");
            }
            Check(scene.staticLayers.size() == 1 && scene.staticLayers[0].statics.colliders.size() == 2, "subLayer resolved and read as a static layer");
        }

        void TestEntityIds(const ImportedScene& scene)
        {
            fmt::print("\n-- SceneEditor: entity ids on import / clone / undo --\n");
            SceneEditor editor;
            int loads = 0;
            editor.SetGpuHooks([&loads](const std::string&) { ++loads; return std::make_shared<Model>(); },
                               [](const std::shared_ptr<Model>&) {},
                               [](const glm::vec3&) {});
            ModelAndInstanceData& data = editor.ModelData();

            const SceneEditor::ImportResult r = editor.ImportScene(scene);
            Check(r.instances == 3 && r.staticLayers == 1, "3 instances + 1 static layer imported");
            Check(data.miModelInstances.size() == 4, "static layer adds one display instance");
            Check(data.miStaticLayers.size() == 1 && data.miStaticLayers[0].model != nullptr && data.IsStaticLayerModel(data.miStaticLayers[0].model.get()),
                  "static layer registered with its display model");
            if (data.miModelInstances.size() == 4)
            {
                const EntityId a = data.miModelInstances[0]->GetEntityId();
                const EntityId b = data.miModelInstances[1]->GetEntityId();
                const EntityId c = data.miModelInstances[2]->GetEntityId();
                const EntityId d = data.miModelInstances[3]->GetEntityId();
                Check(a == 17, "file id kept");
                Check(b == 18 && r.newIds == 1, "legacy instance gets the next free id");
                Check(c == 19 && r.reassignedIds == 1, "duplicate file id replaced by a fresh one");
                Check(d != kInvalidEntityId && d != a && d != b && d != c, "static display instance gets a unique id too");
                Check(data.miModelInstances[0]->GetAgentConfig() && data.miModelInstances[0]->GetAgentConfig()->possessed, "first possessed agent stays possessed");
                Check(data.miModelInstances[2]->GetAgentConfig() && !data.miModelInstances[2]->GetAgentConfig()->possessed, "second possessed agent is cleared");
                Check(editor.FindEntity(17) == data.miModelInstances[0], "FindEntity");

                const std::shared_ptr<ModelInstance> clone = editor.CloneInstance(data.miModelInstances[0]);
                const EntityId cloneId = clone->GetEntityId();
                Check(cloneId != kInvalidEntityId && cloneId != a && cloneId != b && cloneId != c && cloneId != d, "clone gets a new id");
                Check(clone->GetAgentConfig() && !clone->GetAgentConfig()->possessed && clone->GetAgentConfig()->tree == "behavior/guard.behaviortree.usda",
                      "clone copies the agent config, not the possession");
                editor.Undo();
                Check(editor.FindEntity(cloneId) == nullptr, "undo clone removes it");
                editor.Redo();
                Check(editor.FindEntity(cloneId) == clone, "redo restores the same object with the same id");
                editor.DeleteInstance(data.miModelInstances[1]);
                editor.Undo();
                Check(editor.FindEntity(18) != nullptr, "undo delete keeps the id");
                const EntityId next = editor.AllocateEntityId();
                Check(next > cloneId, "ids are never reused within a session");
            }

            const int loadsBefore = loads;
            const SceneEditor::ImportResult again = editor.ImportScene(scene);
            Check(again.staticLayers == 0, "importing the same level twice does not duplicate its static layer");
            Check(again.reassignedIds == 2, "second import: both authored ids already taken -> reassigned");
            Check(loads > loadsBefore, "assets are loaded through the hook");

            // history: Import, Clone, Import (the undone Delete was dropped by the second import)
            editor.Undo();
            editor.Undo();
            Check(data.miStaticLayers.size() == 1, "undoing the second import and the clone keeps the first import");
            editor.Undo();
            Check(data.miStaticLayers.empty() && data.miModelInstances.empty() && data.miModelList.empty(),
                  "undoing the first import removes its instances, models and static layer");

            // Same shutdown contract as Application::Cleanup: discard the history while the editor's hooks
            // are alive (Editor's destructor would run the discard callbacks after them).
            editor.ReleaseAllGpuResources();
        }
    } // namespace

    int RunLevelSelfTests()
    {
        gChecks = 0;
        gFails = 0;
        fmt::print("\n== Level IO self tests ==\n");

        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ranim_level_selftest";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        WriteFile(dir / "maya.static.usda", kMayaStatic);
        WriteFile(dir / "maya_cm.static.usda", kMayaCentimetreStatic);
        WriteFile(dir / "blender.static.usda", kBlenderStatic);
        WriteFile(dir / "asset.usda", kAsset);
        WriteFile(dir / "test.level.usda", kLevel);

        TestMayaStatic(dir);
        TestOtherExporters(dir);
        ImportedScene scene;
        TestLevelImport(dir, scene);
        TestEntityIds(scene);

        fmt::print("\nLevel result: {}/{} passed\n", gChecks - gFails, gChecks);
        return gFails;
    }
} // namespace RAnimation
