#pragma once
#include <memory>
#include <string>
#include <vector>

#include <Platform/SDL/SDLPlatform.h>
#include <Renderer/Renderer.h>
#include <Renderer/UserInterface.h>
#include <Editor/SceneEditor.h>
#include <AnimGraph/AnimationSystem.h>
#include <Controls/CharacterController.h>
#include <Collision/CollisionSystem.h>

namespace RAnimation
{
    // Command-line smoke test (see Program.cpp). Empty / negative values mean "not set".
    struct SmokeTestOptions
    {
        std::string scenePath;
        std::string evalMode;  // cpu | a | b | c
        std::string graphPath;
        int crowd = 0;
        int frames = -1;
        bool verify = false; // compare GPU skinning matrices with the CPU pose at the final frame
        float fixedDeltaTime = 0.0f;       // > 0: fixed frame delta (deterministic runs)
        bool controller = false;           // attach the character state machine to the first instance + scripted input
        std::string saveStateMachinePath;  // write the built-in character definition here and continue
        bool noCollision = false;          // skip CollisionSystem::Init (bodies are never created)
        bool collisionDebugDraw = false;   // start with "Show AABB" + "Show shapes" on (exercises the line path)
        std::string collisionThreads;      // auto | single | pool (Jolt job system for PhysicsSystem::Update)
        int collisionThreshold = -1;       // auto mode: bodies >= threshold -> thread pool (-1 = default)
        bool collisionBench = false;       // run the collision benchmark instead of the main loop (needs --scene)
        std::string collisionBenchSizes;   // comma-separated instance counts (default 1,2,4,...,256)
        int collisionBenchFrames = 300;
    };

    class Application final
    {
    public:
        bool init(unsigned int width, unsigned int height, std::string title);
        // Applies the smoke-test options after init (scene import, tier, graph, crowd). Returns false on error.
        bool ApplySmokeTestOptions(const SmokeTestOptions& options);
        void MainLoop();
        void Cleanup();
    private:
        void printFrameStats(const char* tag) const;
        void runVerification();
        // Keyboard -> CharacterInput / events for the attached character (ImGui IO, like the cameras).
        void updateCharacterInput();
        // --controller: scripted input by frame number; verifies the transition log at the end.
        void runControllerScript(int frame);
        void verifyControllerLog();
        // Collision stage of one frame: Sync -> UpdateBodies -> Step -> Resolve -> pushes -> debug lines.
        void updateCollision(float deltaTime);
        // --collision-bench: spawn N instances (spread / packed), time the collision stage single vs pool.
        void runCollisionBench();

        int mFrameLimit = -1;
        int mFramesRendered = 0;
        bool mVerifyAtEnd = false;
        bool mVerifyFailed = false;
        float mFixedDeltaTime = 0.0f;
        bool mControllerScript = false;
        bool mCollisionBench = false;
        glm::vec3 mControllerStart = glm::vec3(0.0f); // --controller: position at frame 0 (motion assertions)
        std::vector<int> mCollisionBenchSizes;
        int mCollisionBenchFrames = 300;

    public:
        bool VerificationFailed() const { return mVerifyFailed; }

    private:

        std::unique_ptr<Renderer> mRenderer = nullptr;
        std::unique_ptr<SDLPlatform> mPlatform = nullptr;

        // Editor control layer (owns scene data + undo/redo + mode) and the UI that drives it. The
        // Renderer knows about neither; the Application is the only place they meet.
        SceneEditor mSceneEditor{};
        UserInterface mUserInterface{};

        // Explicit per-frame animation stage (graph update, CPU seams, Work records). GPU-free; the
        // renderer only packs what this produced.
        AnimationSystem mAnimationSystem{};

        // Controllable character: state machine on top of the selected instance's AnimGraph. Ticked
        // right before the animation stage so the graph it chose is the one evaluated this frame.
        CharacterController mCharacterController{};

        // Character-vs-character collision (Jolt). Runs after the animation stage with this frame's CPU poses
        // and writes its pushes back into the instances before the renderer reads their transforms.
        CollisionSystem mCollisionSystem{};

        // Window title without the mode suffix; a "  [edit]" tag is appended while in Edit mode.
        std::string mBaseTitle;
    };
} // namespace RAnimation