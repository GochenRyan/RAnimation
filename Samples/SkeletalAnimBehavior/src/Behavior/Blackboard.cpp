#include <Behavior/Blackboard.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

#include <fmt/format.h>

namespace RAnimation
{
    const char* BbTypeName(BbType type)
    {
        switch (type)
        {
            case BbType::Bool: return "Bool";
            case BbType::Int: return "Int";
            case BbType::Float: return "Float";
            case BbType::Vec3: return "Vec3";
            case BbType::String: return "String";
            case BbType::Entity: return "Entity";
        }
        return "?";
    }

    bool ParseBbType(const std::string& name, BbType& out)
    {
        for (BbType t : {BbType::Bool, BbType::Int, BbType::Float, BbType::Vec3, BbType::String, BbType::Entity})
        {
            if (name == BbTypeName(t))
            {
                out = t;
                return true;
            }
        }
        return false;
    }

    const char* BbScopeName(BbScope scope)
    {
        switch (scope)
        {
            case BbScope::Global: return "Global";
            case BbScope::TreeShared: return "TreeShared";
            case BbScope::Instance: return "Instance";
            case BbScope::Node: return "Node";
        }
        return "?";
    }

    bool ParseBbScope(const std::string& name, BbScope& out)
    {
        for (BbScope s : {BbScope::Global, BbScope::TreeShared, BbScope::Instance, BbScope::Node})
        {
            if (name == BbScopeName(s))
            {
                out = s;
                return true;
            }
        }
        return false;
    }

    BbType TypeOf(const BbValue& value)
    {
        return static_cast<BbType>(value.index());
    }

    BbValue DefaultValueFor(BbType type)
    {
        switch (type)
        {
            case BbType::Bool: return false;
            case BbType::Int: return 0;
            case BbType::Float: return 0.0f;
            case BbType::Vec3: return glm::vec3(0.0f);
            case BbType::String: return std::string();
            case BbType::Entity: return EntityId{0};
        }
        return 0.0f;
    }

    float ValueAsFloat(const BbValue& value)
    {
        switch (TypeOf(value))
        {
            case BbType::Bool: return std::get<bool>(value) ? 1.0f : 0.0f;
            case BbType::Int: return static_cast<float>(std::get<int>(value));
            case BbType::Float: return std::get<float>(value);
            case BbType::Entity: return static_cast<float>(std::get<EntityId>(value));
            default: return 0.0f;
        }
    }

    bool ConvertValue(const BbValue& in, BbType to, BbValue& out)
    {
        const BbType from = TypeOf(in);
        if (from == to)
        {
            out = in;
            return true;
        }
        const bool numericFrom = from == BbType::Bool || from == BbType::Int || from == BbType::Float;
        switch (to)
        {
            case BbType::Bool:
                if (numericFrom)
                {
                    out = ValueAsFloat(in) != 0.0f;
                    return true;
                }
                if (from == BbType::Entity)
                {
                    out = std::get<EntityId>(in) != 0;
                    return true;
                }
                return false;
            case BbType::Int:
                if (numericFrom)
                {
                    out = static_cast<int>(std::lround(ValueAsFloat(in)));
                    return true;
                }
                return false;
            case BbType::Float:
                if (numericFrom)
                {
                    out = ValueAsFloat(in);
                    return true;
                }
                return false;
            case BbType::Entity:
                if (from == BbType::Int)
                {
                    out = static_cast<EntityId>(std::max(0, std::get<int>(in)));
                    return true;
                }
                return false;
            default:
                return false;
        }
    }

    bool ValuesEqual(const BbValue& a, const BbValue& b)
    {
        if (a.index() != b.index())
        {
            return false;
        }
        if (const glm::vec3* va = std::get_if<glm::vec3>(&a))
        {
            const glm::vec3& vb = std::get<glm::vec3>(b);
            return std::abs(va->x - vb.x) <= 1e-4f && std::abs(va->y - vb.y) <= 1e-4f && std::abs(va->z - vb.z) <= 1e-4f;
        }
        return a == b;
    }

    std::string ValueToString(const BbValue& value)
    {
        switch (TypeOf(value))
        {
            case BbType::Bool: return std::get<bool>(value) ? "true" : "false";
            case BbType::Int: return std::to_string(std::get<int>(value));
            case BbType::Float: return fmt::format("{:g}", std::get<float>(value));
            case BbType::Vec3:
            {
                const glm::vec3& v = std::get<glm::vec3>(value);
                return fmt::format("{:g}, {:g}, {:g}", v.x, v.y, v.z);
            }
            case BbType::String: return std::get<std::string>(value);
            case BbType::Entity: return "#" + std::to_string(std::get<EntityId>(value));
        }
        return {};
    }

    bool ValueFromString(BbType type, const std::string& text, BbValue& out)
    {
        std::string t = text;
        while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front())))
            t.erase(t.begin());
        while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back())))
            t.pop_back();
        char* end = nullptr;
        switch (type)
        {
            case BbType::Bool:
                if (t == "true" || t == "1")
                    out = true;
                else if (t == "false" || t == "0" || t.empty())
                    out = false;
                else
                    return false;
                return true;
            case BbType::Int:
            {
                const long v = std::strtol(t.c_str(), &end, 10);
                if (end == t.c_str())
                    return false;
                out = static_cast<int>(v);
                return true;
            }
            case BbType::Float:
            {
                const float v = std::strtof(t.c_str(), &end);
                if (end == t.c_str())
                    return false;
                out = v;
                return true;
            }
            case BbType::Vec3:
            {
                glm::vec3 v(0.0f);
                std::string s = t;
                for (char& c : s)
                {
                    if (c == ',' || c == '(' || c == ')')
                        c = ' ';
                }
                std::istringstream in(s);
                if (!(in >> v.x >> v.y >> v.z))
                    return false;
                out = v;
                return true;
            }
            case BbType::String: out = t; return true;
            case BbType::Entity:
            {
                const char* p = t.c_str();
                if (*p == '#')
                    ++p;
                const unsigned long long v = std::strtoull(p, &end, 10);
                if (end == p)
                    return false;
                out = static_cast<EntityId>(v);
                return true;
            }
        }
        return false;
    }

    void BlackboardStore::Init(const std::vector<BlackboardKeyDecl>& decls)
    {
        mDecls = decls;
        ResetToDefaults();
    }

    void BlackboardStore::ResetToDefaults()
    {
        mValues.clear();
        mSet.assign(mDecls.size(), 0);
        for (const BlackboardKeyDecl& d : mDecls)
        {
            BbValue v;
            if (!ConvertValue(d.defaultValue, d.type, v))
            {
                v = DefaultValueFor(d.type);
            }
            mValues.push_back(std::move(v));
        }
    }

    int BlackboardStore::Find(const std::string& name) const
    {
        for (size_t i = 0; i < mDecls.size(); ++i)
        {
            if (mDecls[i].name == name)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    bool BlackboardStore::IsSet(uint32_t slot) const
    {
        if (slot >= mSet.size() || mSet[slot] == 0)
        {
            return false;
        }
        if (const EntityId* e = std::get_if<EntityId>(&mValues[slot]))
        {
            return *e != 0;
        }
        return true;
    }

    bool BlackboardStore::Set(uint32_t slot, const BbValue& value, EntityId writer)
    {
        if (slot >= mValues.size())
        {
            return false;
        }
        BbValue converted;
        if (!ConvertValue(value, mDecls[slot].type, converted))
        {
            return false;
        }
        const bool changed = !ValuesEqual(mValues[slot], converted) || mSet[slot] == 0;
        mValues[slot] = std::move(converted);
        mSet[slot] = 1;
        if (changed && mDecls[slot].wakeOnChange && onChange)
        {
            onChange(slot, writer);
        }
        return changed;
    }

    bool BlackboardStore::Clear(uint32_t slot, EntityId writer)
    {
        if (slot >= mValues.size())
        {
            return false;
        }
        BbValue def;
        if (!ConvertValue(mDecls[slot].defaultValue, mDecls[slot].type, def))
        {
            def = DefaultValueFor(mDecls[slot].type);
        }
        const bool changed = mSet[slot] != 0 || !ValuesEqual(mValues[slot], def);
        mValues[slot] = std::move(def);
        mSet[slot] = 0;
        if (changed && mDecls[slot].wakeOnChange && onChange)
        {
            onChange(slot, writer);
        }
        return changed;
    }

    bool BlackboardStore::SetByName(const std::string& name, const BbValue& value, EntityId writer)
    {
        const int slot = Find(name);
        return slot >= 0 && Set(static_cast<uint32_t>(slot), value, writer);
    }
} // namespace RAnimation
