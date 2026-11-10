#include <cstdlib>
#include <cstring>
#include <string>

#include <fmt/base.h>

#include <Application/Application.h>
#include <StateMachine/StateMachineSelfTest.h>
#include <Collision/CollisionSelfTest.h>

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
//   --selftest                run the state-machine + collision self tests (no window, no GPU) and exit with the failure count
//   --no-collision            do not create the Jolt collision world
//   --collision-debug-draw    start with the collision AABB + shape wireframes visible
//   --collision-threads m     auto | single | pool: job system for PhysicsSystem::Update (auto switches by body count)
//   --collision-threshold N   auto mode: use the thread pool once there are N or more bodies
//   --collision-bench         benchmark the collision stage (needs --scene; prints a Markdown table, then exits)
//   --collision-bench-sizes   comma-separated instance counts for the benchmark (default 1,2,4,...,256)
//   --collision-bench-frames  frames per benchmark cell (default 300)
int main(int argc, char* argv[])
{
    SmokeTestOptions smoke;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--selftest")
        {
            return RunStateMachineSelfTests() + RunCollisionSelfTests();
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
