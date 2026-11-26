#pragma once
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <Agent/IDecisionMaker.h>
#include <Behavior/BehaviorTreeAsset.h>
#include <Behavior/Blackboard.h>
#include <Behavior/NodeTypes.h>
#include <Model/EntityId.h>

// Owns what behaviour trees share: the node type registry, the compiled tree cache, the Global store
// (behavior/global.blackboard.usda) and one TreeShared store per tree. Routes wake-ups for shared keys to
// the agents that use them (through `wake`, so this module never sees CharacterSystem).

namespace RAnimation
{
    class BehaviorTreeBrain;

    class BehaviorSystem
    {
    public:
        static constexpr const char* kGlobalSchemaRelPath = "behavior/global.blackboard.usda";

        // behaviorRoot = Assets/SkeletalAnimBehavior (tree paths in agent configs are relative to it).
        bool Init(const std::string& behaviorRoot, std::string& outError);
        const std::string& Root() const { return mRoot; }
        std::string AbsolutePath(const std::string& relativePath) const;
        std::string RelativePath(const std::string& absolutePath) const;

        const NodeTypeRegistry& Types() const { return mTypes; }

        // Global blackboard: schema (editable) + store.
        const BlackboardSchema& GlobalSchema() const { return mGlobalSchema; }
        BlackboardStore& Global() { return mGlobal; }
        // Replaces the schema; values of keys that survive (same name and type) are kept.
        void SetGlobalSchema(const BlackboardSchema& schema);
        bool SaveGlobalSchema(std::string& outError) const;

        // Compiled tree for a relative path (cached). Errors list validation problems.
        std::shared_ptr<const CompiledTree> LoadTree(const std::string& relativePath, std::vector<ValidationError>& errors);
        // Edited tree (Edit -> View): replaces the cache entry and rebuilds the TreeShared store to the new
        // schema's defaults (13.1 #4). Callers then rebuild the brains of the agents running it.
        void ReplaceTree(const std::string& relativePath, std::shared_ptr<const CompiledTree> tree);
        std::shared_ptr<const CompiledTree> Cached(const std::string& relativePath) const;
        std::vector<std::string> CachedPaths() const;
        void ForgetTree(const std::string& relativePath) { mTrees.erase(relativePath); }

        BlackboardStore* TreeShared(const std::string& relativePath);
        const std::map<std::string, std::unique_ptr<BlackboardStore>>& TreeSharedStores() const { return mTreeShared; }

        // Brain factory for CharacterSystem: BehaviorTreeBrain for config.tree, IdleBrain when empty / broken.
        std::unique_ptr<IDecisionMaker> CreateBrain(const AgentConfig& config, EntityId self, std::string& outError);

        // wake(entity): the agent should tick next frame (a shared key it uses changed)
        std::function<void(EntityId)> wake;

        // brain bookkeeping for wake routing (called by BehaviorTreeBrain)
        void Register(BehaviorTreeBrain* brain);
        void Unregister(BehaviorTreeBrain* brain);
        const std::set<BehaviorTreeBrain*>& Brains() const { return mBrains; }

    private:
        void OnTreeSharedChange(const std::string& path, EntityId writer);
        void OnGlobalChange(uint32_t slot, EntityId writer);

        std::string mRoot;
        NodeTypeRegistry mTypes;
        BlackboardSchema mGlobalSchema;
        BlackboardStore mGlobal;
        std::map<std::string, std::shared_ptr<const CompiledTree>> mTrees;
        std::map<std::string, std::unique_ptr<BlackboardStore>> mTreeShared;
        std::set<BehaviorTreeBrain*> mBrains;
    };

    // .behaviortree.usda / global.blackboard.usda (Behavior/BehaviorTreeUsdIo.cpp, the only pxr TU here).
    bool ReadTreeDesc(const std::string& path, TreeDesc& out, std::string& outError);
    bool WriteTreeDesc(const TreeDesc& desc, const NodeTypeRegistry& types, const std::string& path, std::string& outError);
    bool ReadBlackboardSchema(const std::string& path, BlackboardSchema& out, std::string& outError);
    bool WriteBlackboardSchema(const BlackboardSchema& schema, const std::string& path, std::string& outError);
    // Checkpoint: SaveNode tree <-> .behaviorsave.usda (one Scope per node, ranim:<key> attributes).
    bool WriteSaveTree(const SaveNode& root, const std::string& path, std::string& outError);
    bool ReadSaveTree(const std::string& path, SaveNode& out, std::string& outError);
} // namespace RAnimation
