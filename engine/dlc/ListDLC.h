#pragma once

#include "DLCCommon.h"

#include <algorithm>

namespace cuff
{

    // ---- DLC:list ----
    // Functional-style helpers: all of these return a *new* list/value and
    // never mutate the argument, so they behave predictably regardless of
    // list's reference semantics (see Value.h) — no aliasing surprises from
    // calling a library function.
    inline void registerListDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        reg["length"] = nativeLength;
        reg["contains"] = nativeContains;
        reg["index_of"] = nativeIndexOf;

        reg["list_sort"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("list_sort", args, 1, loc);
            const auto &src = expectList("list_sort", args, 0, loc).items;
            auto out = std::make_shared<ValueList>();
            out->items = src;
            bool allNumbers = std::all_of(src.begin(), src.end(), [](const Value &v)
                                          { return v.isNumber(); });
            bool allStrings = std::all_of(src.begin(), src.end(), [](const Value &v)
                                          { return v.isStr(); });
            if (allNumbers)
            {
                if (std::any_of(src.begin(), src.end(), [](const Value &v)
                                { return std::isnan(v.asNumber()); }))
                    throw ValueError("list_sort() cannot order a list that contains NaN", loc);
                std::sort(out->items.begin(), out->items.end(), [](const Value &a, const Value &b)
                          { return a.asNumber() < b.asNumber(); });
            }
            else if (allStrings)
            {
                std::sort(out->items.begin(), out->items.end(), [](const Value &a, const Value &b)
                          { return a.asStr() < b.asStr(); });
            }
            else
            {
                throw TypeError("list_sort() requires a list of all numbers or all strings (mixed/other types aren't orderable)", loc);
            }
            return Value::makeList(std::move(out));
        };

        reg["list_reverse"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("list_reverse", args, 1, loc);
            auto out = std::make_shared<ValueList>();
            out->items = expectList("list_reverse", args, 0, loc).items;
            std::reverse(out->items.begin(), out->items.end());
            return Value::makeList(std::move(out));
        };

        reg["list_join"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("list_join", args, 2, loc);
            const auto &items = expectList("list_join", args, 0, loc).items;
            const std::string &sep = expectStr("list_join", args, 1, loc);
            size_t total = items.empty() ? 0 : sep.size() * (items.size() - 1);
            for (size_t i = 0; i < items.size(); ++i)
            {
                if (!items[i].isStr())
                    throw TypeError("list_join() requires every element to be a str (index " + std::to_string(i + 1) +
                                        " is a " + valueTypeName(items[i].type()) + ") — use convert:to_str() first",
                                    loc);
                total += items[i].asStr().size();
                ensureStringSize(total, loc);
            }
            std::string out;
            out.reserve(total);
            for (size_t i = 0; i < items.size(); ++i)
            {
                if (i)
                    out += sep;
                out += items[i].asStr();
            }
            return Value::makeStr(std::move(out));
        };

        reg["list_unique"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("list_unique", args, 1, loc);
            const auto &src = expectList("list_unique", args, 0, loc).items;
            auto out = std::make_shared<ValueList>();
            bool allNumbers = std::all_of(src.begin(), src.end(), [](const Value &v)
                                          { return v.isNumber(); });
            bool allStrings = std::all_of(src.begin(), src.end(), [](const Value &v)
                                          { return v.isStr(); });
            if (allNumbers)
            {
                std::unordered_set<double> seen;
                for (const auto &v : src)
                    if (seen.insert(v.asNumber()).second)
                        out->items.push_back(v);
            }
            else if (allStrings)
            {
                std::unordered_set<std::string_view> seen;
                for (const auto &v : src)
                    if (seen.insert(std::string_view(v.asStr())).second)
                        out->items.push_back(v);
            }
            else
            {
                for (const auto &v : src)
                {
                    bool dup = std::any_of(out->items.begin(), out->items.end(), [&](const Value &existing)
                                           { return valuesEqual(existing, v, loc); });
                    if (!dup)
                        out->items.push_back(v);
                }
            }
            return Value::makeList(std::move(out));
        };

        reg["list_sum"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("list_sum", args, 1, loc);
            double total = 0;
            for (const auto &v : expectList("list_sum", args, 0, loc).items)
            {
                if (!v.isNumber())
                    throw TypeError("list_sum() requires a list of numbers, found a " + valueTypeName(v.type()), loc);
                total += v.asNumber();
            }
            return Value::makeNumber(total);
        };

        reg["list_average"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("list_average", args, 1, loc);
            const auto &items = expectList("list_average", args, 0, loc).items;
            if (items.empty())
                throw ValueError("list_average() cannot take an empty list", loc);
            double total = 0;
            for (const auto &v : items)
            {
                if (!v.isNumber())
                    throw TypeError("list_average() requires a list of numbers, found a " + valueTypeName(v.type()), loc);
                total += v.asNumber();
            }
            return Value::makeNumber(total / static_cast<double>(items.size()));
        };

        // Expands one level of nesting; non-list elements are kept as they are.
        reg["list_flatten"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("list_flatten", args, 1, loc);
            auto out = std::make_shared<ValueList>();
            for (const auto &v : expectList("list_flatten", args, 0, loc).items)
            {
                if (v.isList())
                {
                    ensureItemCount(out->items.size() + v.asList()->items.size(), loc);
                    out->items.insert(out->items.end(), v.asList()->items.begin(), v.asList()->items.end());
                }
                else
                {
                    ensureItemCount(out->items.size() + 1, loc);
                    out->items.push_back(v);
                }
            }
            return Value::makeList(std::move(out));
        };

        // Inclusive on both ends, like `loop repeat`; counts down when start > end.
        reg["list_range"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("list_range", args, 2, 3, loc);
            double start = expectNumber("list_range", args, 0, loc);
            double end = expectNumber("list_range", args, 1, loc);
            if (!std::isfinite(start) || !std::isfinite(end))
                throw ValueError("list_range() requires finite numbers", loc);
            double step = start <= end ? 1.0 : -1.0;
            if (args.size() == 3)
            {
                step = expectNumber("list_range", args, 2, loc);
                if (!std::isfinite(step) || step == 0.0)
                    throw ValueError("list_range()'s step must be a non-zero finite number", loc);
            }
            auto out = std::make_shared<ValueList>();
            if ((step > 0 && start > end) || (step < 0 && start < end))
                return Value::makeList(std::move(out));
            double count = std::floor((end - start) / step) + 1;
            if (!(count <= static_cast<double>(limits::kMaxCollectionItems)))
                throw CuffRuntimeError(ErrorCode::SizeLimitExceeded, "collection exceeds the maximum allowed size", loc);
            size_t n = static_cast<size_t>(count);
            out->items.reserve(n);
            for (size_t i = 0; i < n; ++i)
                out->items.push_back(Value::makeNumber(start + static_cast<double>(i) * step));
            return Value::makeList(std::move(out));
        };
    }

} // namespace cuff
