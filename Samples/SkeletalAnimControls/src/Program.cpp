#include <cstdlib>
#include <cstring>
#include <string>

#include <fmt/base.h>

#include <Application/Application.h>
#include <StateMachine/StateMachineSelfTest.h>

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
//   --selftest                run the state-machine self test (no window, no GPU) and exit with the failure count
int main(int argc, char* argv[])
{
    SmokeTestOptions smoke;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--selftest")
        {
            return RunStateMachineSelfTests();
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
