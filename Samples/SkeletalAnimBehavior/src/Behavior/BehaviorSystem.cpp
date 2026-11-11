#include <Behavior/BehaviorSystem.h>

#include <algorithm>
#include <filesystem>

#include <fmt/base.h>
#include <fmt/color.h>
#include <fmt/format.h>

#include <Behavior/BehaviorTreeBrain.h>

namespace RAnimation
{
    bool BehaviorSystem::Init(const std::string& behaviorRoot, std::string& outError)
    {
        mRoot = std::filesystem::path(behaviorRoot).lexically_normal().generic_string();
        mGlobalSchema = BlackboardSchema{};
        const std::string globalPath = AbsolutePath(kGlobalSchemaRelPath);
        std::error_code ec;
        if (std::filesystem::exists(globalPath, ec))
        {
            std::string error;
            if (!ReadBlackboardSchema(globalPath, mGlobalSchema, error))
            {
                outError = error;
                return false;
            }
            for (BlackboardKeyDecl& k : mGlobalSchema.keys)
            {
                k.scope = BbScope::Global;
            }
        }
        mGlobal.Init(mGlobalSchema);
        mGlobal.onChange = [this](uint32_t slot, EntityId writer) { OnGlobalChange(slot, writer); };
        return true;
    }

    std::string BehaviorSystem::AbsolutePath(const std::string& relativePath) const
    {
        const std::filesystem::path p(relativePath);
        if (p.is_absolute())
        {
            return p.lexically_normal().generic_string();
        }
        return (std::filesystem::path(mRoot) / p).lexically_normal().generic_string();
    }

    std::string BehaviorSystem::RelativePath(const std::string& absolutePath) const
    {
        std::error_code ec;
        const std::filesystem::path rel = std::filesystem::relative(absolutePath, mRoot, ec);
        if (ec || rel.empty() || rel.generic_string().rfind("..", 0) == 0)
        {
            return std::filesystem::path(absolutePath).lexically_normal().generic_string();
        }
        return rel.generic_string();
    }

    void BehaviorSystem::SetGlobalSchema(const BlackboardSchema& schema)
    {
        BlackboardStore old = std::move(mGlobal);
        mGlobalSchema = schema;
        for (BlackboardKeyDecl& k : mGlobalSchema.keys)
        {
            k.scope = BbScope::Global;
        }
        mGlobal = BlackboardStore{};
        mGlobal.Init(mGlobalSchema);
        for (size_t s = 0; s < old.Size(); ++s)
        {
            const int slot = mGlobal.Find(old.Decl(static_cast<uint32_t>(s)).name);
            if (slot >= 0 && mGlobal.Decl(static_cast<uint32_t>(slot)).type == old.Decl(static_cast<uint32_t>(s)).type &&
                old.IsSet(static_cast<uint32_t>(s)))
            {
                mGlobal.Set(static_cast<uint32_t>(slot), old.Get(static_cast<uint32_t>(s)), kInvalidEntityId);
            }
        }
        mGlobal.onChange = [this](uint32_t slot, EntityId writer) { OnGlobalChange(slot, writer); };
        // global slots are compiled into the trees: recompile what is cached
        for (auto& [path, tree] : mTrees)
        {
            std::vector<ValidationError> errors;
            auto recompiled = CompileTree(tree->desc, path, mTypes, mGlobalSchema, errors);
            if (recompiled)
            {
                tree = recompiled;
            }
            else
            {
                fmt::print(stderr, fg(fmt::color::yellow), "behavior: '{}' no longer compiles against the global schema: {}\n", path,
                           errors.empty() ? "" : errors.front().message);
            }
        }
    }

    bool BehaviorSystem::SaveGlobalSchema(std::string& outError) const
    {
        return WriteBlackboardSchema(mGlobalSchema, AbsolutePath(kGlobalSchemaRelPath), outError);
    }

    std::shared_ptr<const CompiledTree> BehaviorSystem::Cached(const std::string& relativePath) const
    {
        const auto it = mTrees.find(relativePath);
        return it != mTrees.end() ? it->second : nullptr;
    }

    std::vector<std::string> BehaviorSystem::CachedPaths() const
    {
        std::vector<std::string> paths;
        for (const auto& [path, tree] : mTrees)
        {
            paths.push_back(path);
        }
        return paths;
    }

    std::shared_ptr<const CompiledTree> BehaviorSystem::LoadTree(const std::string& relativePath, std::vector<ValidationError>& errors)
    {
        errors.clear();
        if (auto it = mTrees.find(relativePath); it != mTrees.end())
        {
            return it->second;
        }
        auto desc = std::make_shared<TreeDesc>();
        std::string error;
        if (!ReadTreeDesc(AbsolutePath(relativePath), *desc, error))
        {
            errors.push_back({~0u, error});
            return nullptr;
        }
        auto tree = CompileTree(desc, relativePath, mTypes, mGlobalSchema, errors);
        if (!tree)
        {
            return nullptr;
        }
        mTrees[relativePath] = tree;
        BlackboardStore* shared = TreeShared(relativePath);
        shared->Init(tree->treeSharedSchema);
        return tree;
    }

    void BehaviorSystem::ReplaceTree(const std::string& relativePath, std::shared_ptr<const CompiledTree> tree)
    {
        mTrees[relativePath] = tree;
        // 13.1 #4: the shared store follows the new schema, at its defaults
        TreeShared(relativePath)->Init(tree->treeSharedSchema);
    }

    BlackboardStore* BehaviorSystem::TreeShared(const std::string& relativePath)
    {
        auto it = mTreeShared.find(relativePath);
        if (it == mTreeShared.end())
        {
            auto store = std::make_unique<BlackboardStore>();
            if (auto tree = Cached(relativePath))
            {
                store->Init(tree->treeSharedSchema);
            }
            store->onChange = [this, relativePath](uint32_t, EntityId writer) { OnTreeSharedChange(relativePath, writer); };
            it = mTreeShared.emplace(relativePath, std::move(store)).first;
        }
        return it->second.get();
    }

    std::unique_ptr<IDecisionMaker> BehaviorSystem::CreateBrain(const AgentConfig& config, EntityId self, std::string& outError)
    {
        if (config.tree.empty())
        {
            return std::make_unique<IdleBrain>();
        }
        std::vector<ValidationError> errors;
        auto tree = LoadTree(config.tree, errors);
        if (!tree)
        {
            outError = fmt::format("tree '{}' not usable: {}", config.tree, errors.empty() ? "?" : errors.front().message);
            return std::make_unique<IdleBrain>();
        }
        return std::make_unique<BehaviorTreeBrain>(this, tree, self, config.tickHz);
    }

    void BehaviorSystem::Register(BehaviorTreeBrain* brain) { mBrains.insert(brain); }
    void BehaviorSystem::Unregister(BehaviorTreeBrain* brain) { mBrains.erase(brain); }

    void BehaviorSystem::OnTreeSharedChange(const std::string& path, EntityId writer)
    {
        if (!wake)
        {
            return;
        }
        for (BehaviorTreeBrain* b : mBrains)
        {
            if (b->Tree()->path == path && b->Self() != writer)
            {
                wake(b->Self());
            }
        }
    }

    void BehaviorSystem::OnGlobalChange(uint32_t slot, EntityId writer)
    {
        if (!wake)
        {
            return;
        }
        for (BehaviorTreeBrain* b : mBrains)
        {
            const std::vector<uint32_t>& used = b->Tree()->globalSlotsUsed;
            if (b->Self() != writer && std::find(used.begin(), used.end(), slot) != used.end())
            {
                wake(b->Self());
            }
        }
    }
} // namespace RAnimation
