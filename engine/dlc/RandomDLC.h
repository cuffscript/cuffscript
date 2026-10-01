#pragma once

#include "DLCCommon.h"

#include <random>

namespace cuff
{

    // ---- DLC:random ----
    inline void registerRandomDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        // One generator shared by every registration, so `use DLC:random` in
        // several modules doesn't reseed it and random_seed() applies everywhere.
        static const std::shared_ptr<std::mt19937_64> rng =
            std::make_shared<std::mt19937_64>(std::random_device{}());

        reg["random_float"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("random_float", args, 0, loc);
            std::uniform_real_distribution<double> dist(0.0, 1.0);
            return Value::makeNumber(dist(*rng));
        };
        reg["random_int"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("random_int", args, 2, loc);
            long long lo = expectWhole("random_int", args, 0, loc);
            long long hi = expectWhole("random_int", args, 1, loc);
            if (lo > hi)
                throw ValueError("random_int() expects the first argument to be <= the second", loc);
            std::uniform_int_distribution<long long> dist(lo, hi);
            return Value::makeNumber(static_cast<double>(dist(*rng)));
        };
        reg["random_seed"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("random_seed", args, 1, loc);
            long long seed = expectWhole("random_seed", args, 0, loc);
            rng->seed(static_cast<unsigned long long>(seed));
            return Value::makeEmpty();
        };
        reg["random_choice"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("random_choice", args, 1, loc);
            const auto &items = expectList("random_choice", args, 0, loc).items;
            if (items.empty())
                throw ValueError("random_choice() cannot pick from an empty list", loc);
            std::uniform_int_distribution<size_t> dist(0, items.size() - 1);
            return items[dist(*rng)];
        };
        reg["random_shuffle"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("random_shuffle", args, 1, loc);
            auto out = std::make_shared<ValueList>();
            out->items = expectList("random_shuffle", args, 0, loc).items;
            std::shuffle(out->items.begin(), out->items.end(), *rng);
            return Value::makeList(std::move(out));
        };
    }

} // namespace cuff
