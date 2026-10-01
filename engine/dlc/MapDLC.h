#pragma once

#include "DLCCommon.h"

namespace cuff
{

    // ---- DLC:map ----
    inline void registerMapDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        reg["length"] = nativeLength;
        reg["contains"] = nativeContains;

        reg["map_keys"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("map_keys", args, 1, loc);
            const ValueMap &m = expectMap("map_keys", args, 0, loc);
            auto out = std::make_shared<ValueList>();
            out->items.reserve(m.size());
            for (const auto &k : m.keys())
                out->items.push_back(Value::makeStr(k));
            return Value::makeList(std::move(out));
        };
        reg["map_values"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("map_values", args, 1, loc);
            auto out = std::make_shared<ValueList>();
            out->items = expectMap("map_values", args, 0, loc).values();
            return Value::makeList(std::move(out));
        };
        reg["map_has_key"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("map_has_key", args, 2, loc);
            const ValueMap &m = expectMap("map_has_key", args, 0, loc);
            return Value::makeBool(m.has(expectStr("map_has_key", args, 1, loc)));
        };
        reg["map_entries"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("map_entries", args, 1, loc);
            const ValueMap &m = expectMap("map_entries", args, 0, loc);
            auto out = std::make_shared<ValueList>();
            out->items.reserve(m.size());
            for (size_t i = 0; i < m.size(); ++i)
            {
                auto pair = std::make_shared<ValueList>();
                pair->items.push_back(Value::makeStr(m.keys()[i]));
                pair->items.push_back(m.values()[i]);
                out->items.push_back(Value::makeList(std::move(pair)));
            }
            return Value::makeList(std::move(out));
        };
        // Returns a new map; on duplicate keys the second map wins.
        reg["map_merge"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("map_merge", args, 2, loc);
            const ValueMap &a = expectMap("map_merge", args, 0, loc);
            const ValueMap &b = expectMap("map_merge", args, 1, loc);
            ensureItemCount(a.size() + b.size(), loc);
            auto out = std::make_shared<ValueMap>();
            out->reserve(a.size() + b.size());
            for (size_t i = 0; i < a.size(); ++i)
                out->set(a.keys()[i], a.values()[i]);
            for (size_t i = 0; i < b.size(); ++i)
                out->set(b.keys()[i], b.values()[i]);
            return Value::makeMap(std::move(out));
        };
    }

} // namespace cuff
