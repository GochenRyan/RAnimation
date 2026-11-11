#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <Platform/SDL/SDLPlatform.h>
#include <Renderer/Renderer.h>
#include <Renderer/UserInterface.h>
#include <Editor/SceneEditor.h>
#include <AnimGraph/AnimationSystem.h>
#include <Agent/CharacterSystem.h>
#include <Behavior/BehaviorSystem.h>
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
        std::string levelRoundTripPath;    // export the imported scene here, re-import and compare (needs --scene)
        bool behavior = false;             // behaviour smoke test (needs --scene behavior_test.level.usda)
        bool showBehaviorEditor = false;   // open the behaviour tree editor window at start
        bool editorSmoke = false;          // drive the behaviour tree editor with injected input (needs behavior_test)
        uint64_t selectEntity = 0;         // select this entity's instance at start (cameras / editor follow it)
        bool viewMode = false;             // start in View mode (agents run; the editor starts in Edit)
        int behaviorBench = 0;             // > 0: spawn this many extra tree agents and time the agent stage
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
        // Keyboard -> the possessed agent's PlayerInputBuffer (ImGui IO, like the cameras).
        void updateCharacterInput();
        // ApplicationBehavior.cpp: behaviour system setup, the agent stage of a frame, frame-end requests.
        void initBehavior();
        void updateAgents(float deltaTime);
        void updateBehaviorFrameEnd();
        // --controller: the scripted agent (ScriptBrain) is checked by frame number; the transition log at the end.
        void checkControllerFrame(int frame);
        void verifyControllerLog();
        Agent* scriptAgent();
        // Collision stage of one frame: Sync -> UpdateBodies -> Step -> Resolve -> pushes -> debug lines.
        void updateCollision(float deltaTime);
        // --collision-bench: spawn N instances (spread / packed), time the collision stage single vs pool.
        void runCollisionBench();
        // --level-roundtrip: export -> re-import -> compare entity ids / agents / static layers; clone check.
        bool runLevelRoundTrip(const std::string& outPath);

        int mFrameLimit = -1;
        int mFramesRendered = 0;
        bool mVerifyAtEnd = false;
        bool mVerifyFailed = false;
        float mFixedDeltaTime = 0.0f;
        bool mControllerScript = false;
        EntityId mScriptEntity = kInvalidEntityId; // --controller: the agent run by ScriptBrain
        bool mCollisionBench = false;
        glm::vec3 mControllerStart = glm::vec3(0.0f); // --controller: position at frame 0 (motion assertions)
        std::vector<int> mCollisionBenchSizes;
        int mCollisionBenchFrames = 300;

    public:
        bool VerificationFailed() const { return mVerifyFailed; }

        // Checkpoint (§8.4). slotPath "<dir>/<name>" -> <name>.level.usda + <name>.behaviorsave.usda. Executed at
        // the end of the frame (Draw done), when every system is consistent.
        void RequestCheckpointSave(const std::string& slotPath) { mSaveRequest = slotPath; }
        void RequestCheckpointLoad(const std::string& slotPath) { mLoadRequest = slotPath; }
        float saveRadius = 30.0f; // agents within this distance of the possessed one are saved (D15)

    private:
        bool saveCheckpoint(const std::string& slotPath, std::string& outError);
        bool loadCheckpoint(const std::string& slotPath, std::string& outError);
        std::string mSaveRequest;
        std::string mLoadRequest;
        std::function<void()> mAfterCheckpointLoad; // smoke test: re-install its scripted brains

        // --behavior (ApplicationBehaviorSmoke.cpp)
        struct BehaviorSmoke;
        std::shared_ptr<BehaviorSmoke> mBehaviorSmoke;
        bool setupBehaviorSmoke();
        // --editor-smoke (ApplicationEditorSmoke.cpp): drives the behaviour tree editor with injected input
        struct EditorSmoke;
        std::shared_ptr<EditorSmoke> mEditorSmoke;
        bool setupEditorSmoke();
        // --behavior-bench
        int mBehaviorBench = 0;
        double mBenchSums[5] = {0, 0, 0, 0, 0}; // perception, decision, execution us, ticked, frames
        void spawnBehaviorBench(int count);
        void installBehaviorSmokeBrains();
        void observeBehaviorFrame(int frame);
        void verifyBehaviorSmoke();

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

        // Behaviour trees: node types, compiled tree cache, Global / TreeShared blackboards. Declared before the
        // agents: their brains unregister from it when they are destroyed.
        BehaviorSystem mBehaviorSystem{};

        // Agents (one CharacterController + decision maker each), scheduled, steered and perceiving. Ticked
        // right before the animation stage so the graph inputs they chose are the ones evaluated this frame.
        CharacterSystem mCharacterSystem{};

        // Character-vs-character collision (Jolt). Runs after the animation stage with this frame's CPU poses
        // and writes its pushes back into the instances before the renderer reads their transforms.
        CollisionSystem mCollisionSystem{};

        // Window title without the mode suffix; a "  [edit]" tag is appended while in Edit mode.
        std::string mBaseTitle;
    };
} // namespace RAnimation