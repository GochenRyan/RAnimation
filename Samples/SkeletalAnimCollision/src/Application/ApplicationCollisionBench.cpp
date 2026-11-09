// Application: --collision-bench. Spawns N instances of the scene's first model in two layouts (spread: no
// contacts, packed: dense contacts), runs the animation + collision stage for a fixed number of frames with
// Jolt's single-threaded job system and with its thread pool, and prints one Markdown table. No rendering.
#include <Application/Application.h>

#include <algorithm>
#include <cstdlib>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include <fmt/base.h>
#include <fmt/color.h>

#include <Model/Model.h>
#include <Model/ModelInstance.h>

using namespace RAnimation;

namespace
{
    struct Cell
    {
        double poseUs = 0.0, writeUs = 0.0, updateUs = 0.0, resolveUs = 0.0;
        double manifolds = 0.0;
    };

    double Median(std::vector<double>& v)
    {
        if (v.empty())
        {
            return 0.0;
        }
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    }
} // namespace

void Application::runCollisionBench()
{
    ModelAndInstanceData& scene = mSceneEditor.ModelData();
    if (scene.miModelList.empty() || !mCollisionSystem.IsInitialized())
    {
        fmt::print(stderr, fg(fmt::color::red), "bench: no model or collision world\n");
        mVerifyFailed = true;
        return;
    }
    std::shared_ptr<Model> model = scene.miModelList.front();
    // Root-motion clips ("_RM") carry the mesh metres away from its instance origin, which turns the "spread"
    // layout into a random pile after a few seconds. Bench on in-place clips only (all clips when none qualify).
    std::vector<unsigned int> benchClips;
    for (size_t i = 0; i < model->GetAnimClips().size(); ++i)
    {
        if (model->GetAnimClips()[i]->GetClipName().find("_RM") == std::string::npos)
        {
            benchClips.push_back(static_cast<unsigned int>(i));
        }
    }
    if (benchClips.empty())
    {
        for (size_t i = 0; i < model->GetAnimClips().size(); ++i)
        {
            benchClips.push_back(static_cast<unsigned int>(i));
        }
    }
    const int clipCount = static_cast<int>(benchClips.size());
    const float dt = mFixedDeltaTime > 0.0f ? mFixedDeltaTime : 1.0f / 60.0f;
    const int frames = mCollisionBenchFrames;
    const int warmup = std::min(30, frames / 4);
    const int budget = static_cast<int>(mRenderer->GetRenderData().rdResourceBudget.maxWorldMatrices);

    // Exact N: start from an empty scene.
    while (!scene.miModelInstances.empty())
    {
        mSceneEditor.DeleteInstance(scene.miModelInstances.back());
    }
    mCharacterController.Detach();
    CollisionSystem::Settings& settings = mCollisionSystem.GetSettings();
    settings.enabled = true;
    settings.pushApart = false; // keep the layout stable across the measured frames
    settings.showAABB = false;
    settings.showShapes = false;
    CollisionThreadingPolicy& threading = mCollisionSystem.World().Threading();

    fmt::print("\n## Collision benchmark\n\n");
    fmt::print("model `{}`, eval mode `{}`, {} frames per cell (+{} warm-up), dt {:.4f} s, Jolt thread pool {} threads, hardware {} threads\n\n",
               model->GetModelFileName(),
               EvalModeName(scene.miEvalMode),
               frames,
               warmup,
               dt,
               mCollisionSystem.World().ThreadPoolSize(),
               std::thread::hardware_concurrency());
    fmt::print("| N | layout | manifolds | pose us | write us | update single us | update pool us | pool / single | resolve us |\n");
    fmt::print("|---|---|---|---|---|---|---|---|---|\n");

    struct Row
    {
        int n;
        const char* layout;
        Cell single, pool;
    };
    std::vector<Row> rows;

    for (int n : mCollisionBenchSizes)
    {
        if (n > budget)
        {
            fmt::print("| {} | - | skipped (instance budget {}) |  |  |  |  |  |  |\n", n, budget);
            continue;
        }
        for (int layoutIndex = 0; layoutIndex < 2; ++layoutIndex)
        {
            const bool packed = layoutIndex == 1;
            const float spacing = packed ? 0.3f : 2.0f;
            const int cols = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(n))));
            std::vector<InstanceSettings> spawn;
            spawn.reserve(static_cast<size_t>(n));
            std::srand(1234);
            for (int i = 0; i < n; ++i)
            {
                InstanceSettings s;
                s.mWorldPosition = glm::vec3(static_cast<float>(i % cols) * spacing, 0.0f, static_cast<float>(i / cols) * spacing);
                s.mWorldRotation = glm::vec3(0.0f, static_cast<float>(std::rand() % 360 - 180), 0.0f);
                s.mAnimClipNr = clipCount > 0 ? benchClips[static_cast<size_t>(std::rand() % clipCount)] : 0u;
                s.mAnimPhase = static_cast<float>(std::rand() % 1000) / 1000.0f;
                spawn.push_back(s);
            }
            mSceneEditor.AddInstances(model, spawn, "Bench");
            mCollisionSystem.Sync(scene);

            Row row{n, packed ? "packed 0.3 m" : "spread 2.0 m", {}, {}};
            for (int modeIndex = 0; modeIndex < 2; ++modeIndex)
            {
                threading.mode = modeIndex == 0 ? CollisionThreadingPolicy::Mode::Single : CollisionThreadingPolicy::Mode::Pool;
                std::vector<double> pose, write, update, resolve, manifolds;
                pose.reserve(static_cast<size_t>(frames));
                write.reserve(static_cast<size_t>(frames));
                update.reserve(static_cast<size_t>(frames));
                resolve.reserve(static_cast<size_t>(frames));
                manifolds.reserve(static_cast<size_t>(frames));
                for (int f = 0; f < warmup + frames; ++f)
                {
                    mAnimationSystem.Update(dt, scene);
                    mCollisionSystem.Sync(scene);
                    mCollisionSystem.UpdateBodies(scene);
                    mCollisionSystem.Step(dt);
                    mCollisionSystem.Resolve();
                    if (f >= warmup)
                    {
                        const CollisionFrameTiming& t = mCollisionSystem.LastTiming();
                        pose.push_back(t.poseUs);
                        write.push_back(t.writeUs);
                        update.push_back(t.updateUs);
                        resolve.push_back(t.resolveUs);
                        manifolds.push_back(static_cast<double>(t.manifolds));
                    }
                }
                Cell& cell = modeIndex == 0 ? row.single : row.pool;
                cell.poseUs = Median(pose);
                cell.writeUs = Median(write);
                cell.updateUs = Median(update);
                cell.resolveUs = Median(resolve);
                cell.manifolds = Median(manifolds);
            }
            rows.push_back(row);
            fmt::print("| {} | {} | {:.0f} | {:.0f} | {:.0f} | {:.0f} | {:.0f} | {:.2f} | {:.0f} |\n",
                       n,
                       row.layout,
                       row.single.manifolds,
                       row.single.poseUs,
                       row.single.writeUs,
                       row.single.updateUs,
                       row.pool.updateUs,
                       row.single.updateUs > 0.0 ? row.pool.updateUs / row.single.updateUs : 0.0,
                       row.single.resolveUs);

            while (!scene.miModelInstances.empty())
            {
                mSceneEditor.DeleteInstance(scene.miModelInstances.back());
            }
            mCollisionSystem.Sync(scene);
        }
    }

    // The cost of PhysicsSystem::Update follows the manifold count, not the body count: in the spread layout the
    // pool only adds its fixed scheduling overhead, in the packed layout it pays off quickly. A body-count
    // threshold is therefore a trade between the two. Report both the break-even N (packed gain >= spread
    // penalty from here on) and the clear-win N (packed gain >= 2x the spread penalty from here on).
    auto gainFrom = [&](size_t row, double margin)
    {
        for (size_t j = row; j + 1 < rows.size(); j += 2)
        {
            const double spreadPenalty = std::max(0.0, rows[j].pool.updateUs - rows[j].single.updateUs);
            const double packedGain = rows[j + 1].single.updateUs - rows[j + 1].pool.updateUs;
            if (packedGain <= 0.0 || packedGain < margin * spreadPenalty)
            {
                return false;
            }
        }
        return true;
    };
    int breakEven = -1;
    int clearWin = -1;
    for (size_t i = 0; i + 1 < rows.size(); i += 2)
    {
        if (breakEven < 0 && gainFrom(i, 1.0))
        {
            breakEven = rows[i].n;
        }
        if (clearWin < 0 && gainFrom(i, 2.0))
        {
            clearWin = rows[i].n;
        }
    }
    fmt::print("\n");
    if (rows.size() >= 2)
    {
        const Row& last = rows[rows.size() - 2];
        fmt::print("Spread layout: the pool costs up to {:.0f} us per frame more than single-threaded (N = {}).\n",
                   std::max(0.0, last.pool.updateUs - last.single.updateUs),
                   last.n);
    }
    if (breakEven > 0)
    {
        fmt::print("Break-even `joltThreshold` (packed gain >= spread penalty from this N on): {}\n", breakEven);
    }
    if (clearWin > 0)
    {
        fmt::print("Suggested `joltThreshold` (packed gain >= 2x the spread penalty from this N on): **{}**\n", clearWin);
    }
    else
    {
        fmt::print("The thread pool never clearly paid off over the measured sizes; keep single-threaded (threshold above the largest N).\n");
    }
    fmt::print("Current default: {} (CollisionThreadingPolicy::joltThreshold).\n", threading.joltThreshold);
}
