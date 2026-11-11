#include <cstdlib>
#include <cstring>
#include <string>

#include <fmt/base.h>

#include <Application/Application.h>
#include <StateMachine/StateMachineSelfTest.h>
#include <Collision/CollisionSelfTest.h>
#include <Editor/LevelSelfTest.h>
#include <Behavior/BehaviorSelfTest.h>
#include <Behavior/BehaviorTestAssets.h>

using namespace RAnimation;

// Optional smoke-test switches (all tiers are exercised without touching the UI):
//   --scene <level.usda>      import a scene at startup (same path as File -> Import Scene from USD)
//   --eval cpu|a|b|c          evaluation tier
//   --graph <file.animgraph.usda>  assign this graph to every animated instance
//   --crowd <N>               spawn N extra instances of the first model (grid, random clip/phase)
//   --frames <N>              render N frames, print the GPU/CPU statistics, then exit 0
//   --verify                  compare GPU skinning matrices with the CPU pose at the final frame
//   --fixed-dt <sec>          use a fixed frame delta instead of wall-clock time (deterministic runs)
//   --controller              attach the character state machine to the first instance and drive it with a
//                             scripted input sequence (implies --fixed-dt 1/60 and --frames 420); the
//                             transition log is checked at the end (exit 1 on mismatch)
//   --save-statemachine <f>   write the built-in character state machine as <f> (.statemachine.usda)
//   --selftest                run the state-machine + collision + level IO self tests (no window, no GPU) and exit with the failure count
//   --level-roundtrip <f>     after --scene: export the scene to <f>, re-import it and check entity ids / agent configs /
//                             static layers survive, check a clone gets a new id, print the static layers (exit 1 on mismatch)
//   --no-collision            do not create the Jolt collision world
//   --collision-debug-draw    start with the collision AABB + shape wireframes visible
//   --collision-threads m     auto | single | pool: job system for PhysicsSystem::Update (auto switches by body count)
//   --collision-threshold N   auto mode: use the thread pool once there are N or more bodies
//   --collision-bench         benchmark the collision stage (needs --scene; prints a Markdown table, then exits)
//   --collision-bench-sizes   comma-separated instance counts for the benchmark (default 1,2,4,...,256)
//   --collision-bench-frames  frames per benchmark cell (default 300)
//   --behavior                behaviour smoke test on --scene (behavior_test.level.usda): scripted player, contact /
//                             boundary / sight / zone / patrol / interaction / request lifecycle checks, a checkpoint
//                             save + load mid-run; implies --fixed-dt 1/60 and --frames 1800 (exit 1 on failure)
//   --write-behavior-assets   write the test level's behaviour trees + global blackboard to Assets/SkeletalAnimBehavior/behavior
//   --show-bt-editor          open the behaviour tree editor window at start (follows the selected agent in View mode)
//   --editor-smoke            after --scene behavior_test.level.usda: drive the behaviour tree editor with injected mouse /
//                             key input (add node, refused save / mode switch, error focus, link + rejected link, delete,
//                             reorder by x, undo / redo, save, Edit -> View restarts only the affected agent, live view,
//                             possession release); exit 1 on failure
//   --behavior-bench <N>      after --scene: N extra patrol / watcher agents (View mode, 10 Hz), prints the per-frame cost
//                             of perception, decisions and execution averaged over --frames (default 600)
int main(int argc, char* argv[])
{
    SmokeTestOptions smoke;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--selftest")
        {
            return RunStateMachineSelfTests() + RunCollisionSelfTests() + RunLevelSelfTests() + RunBehaviorSelfTests();
        }
        else if (arg == "--write-behavior-assets")
        {
            BehaviorSystem behavior;
            std::string error;
            if (!behavior.Init(ASSETS_SRC_DIR "/SkeletalAnimBehavior", error) || !WriteBehaviorTestAssets(behavior, error))
            {
                fmt::print(stderr, "--write-behavior-assets: {}\n", error);
                return 1;
            }
            return 0;
        }
        else if (arg == "--behavior")
        {
            smoke.behavior = true;
        }
        else if (arg == "--show-bt-editor")
        {
            smoke.showBehaviorEditor = true;
        }
        else if (arg == "--editor-smoke")
        {
            smoke.editorSmoke = true;
        }
        else if (arg == "--view")
        {
            smoke.viewMode = true;
        }
        else if (arg == "--select" && hasValue)
        {
            smoke.selectEntity = std::strtoull(argv[++i], nullptr, 10);
        }
        else if (arg == "--behavior-bench" && hasValue)
        {
            smoke.behaviorBench = std::atoi(argv[++i]);
        }
        else if (arg == "--no-collision")
        {
            smoke.noCollision = true;
        }
        else if (arg == "--collision-debug-draw")
        {
            smoke.collisionDebugDraw = true;
        }
        else if (arg == "--collision-threads" && hasValue)
        {
            smoke.collisionThreads = argv[++i];
        }
        else if (arg == "--collision-threshold" && hasValue)
        {
            smoke.collisionThreshold = std::atoi(argv[++i]);
        }
        else if (arg == "--collision-bench")
        {
            smoke.collisionBench = true;
        }
        else if (arg == "--collision-bench-sizes" && hasValue)
        {
            smoke.collisionBenchSizes = argv[++i];
        }
        else if (arg == "--collision-bench-frames" && hasValue)
        {
            smoke.collisionBenchFrames = std::atoi(argv[++i]);
        }
        else if (arg == "--fixed-dt" && hasValue)
        {
            smoke.fixedDeltaTime = static_cast<float>(std::atof(argv[++i]));
        }
        else if (arg == "--controller")
        {
            smoke.controller = true;
        }
        else if (arg == "--save-statemachine" && hasValue)
        {
            smoke.saveStateMachinePath = argv[++i];
        }
        else if (arg == "--scene" && hasValue)
        {
            smoke.scenePath = argv[++i];
        }
        else if (arg == "--eval" && hasValue)
        {
            smoke.evalMode = argv[++i];
        }
        else if (arg == "--graph" && hasValue)
        {
            smoke.graphPath = argv[++i];
        }
        else if (arg == "--crowd" && hasValue)
        {
            smoke.crowd = std::atoi(argv[++i]);
        }
        else if (arg == "--frames" && hasValue)
        {
            smoke.frames = std::atoi(argv[++i]);
        }
        else if (arg == "--verify")
        {
            smoke.verify = true;
        }
        else if (arg == "--level-roundtrip" && hasValue)
        {
            smoke.levelRoundTripPath = argv[++i];
        }
        else
        {
            fmt::print(stderr, "unknown argument '{}'\n", arg);
            return 2;
        }
    }

    Application app;
    if (!app.init(1280, 720, "Skeletal Animation Sample"))
    {
        return -1;
    }

    if (!app.ApplySmokeTestOptions(smoke))
    {
        app.Cleanup();
        return 1;
    }

    app.MainLoop();
    const bool verifyFailed = app.VerificationFailed();
    app.Cleanup();

    return verifyFailed ? 1 : 0;
}
