#pragma once
#include "DLCCommon.h"
#include <mutex>
#include <random>
namespace cuff
{
namespace detail
{
// One generator for the whole process. Every call that reads or reseeds it holds the lock, so scripts running on
// different threads cannot corrupt it.
struct RandomState
{
    std::mutex mutex;
    std::mt19937_64 engine{std::random_device{}()};
};

inline RandomState &randomState()
{
    static RandomState state;
    return state;
}
}

inline void registerRandomDLC(std::unordered_map<std::string, NativeFn> &reg)
{
    reg["random_float"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
    {
        expectArgCount("random_float", args, 0, loc);
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        auto &state = detail::randomState();
        std::lock_guard<std::mutex> lock(state.mutex);
        return Value::makeNumber(dist(state.engine));
    };
    reg["random_int"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
    {
        expectArgCount("random_int", args, 2, loc);
        long long lo = expectWhole("random_int", args, 0, loc);
        long long hi = expectWhole("random_int", args, 1, loc);
        if (lo > hi)
            throw ValueError("random_int() expects the first argument to be <= the second", loc);
        std::uniform_int_distribution<long long> dist(lo, hi);
        auto &state = detail::randomState();
        std::lock_guard<std::mutex> lock(state.mutex);
        return Value::makeNumber(static_cast<double>(dist(state.engine)));
    };
    reg["random_seed"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
    {
        expectArgCount("random_seed", args, 1, loc);
        long long seed = expectWhole("random_seed", args, 0, loc);
        auto &state = detail::randomState();
        std::lock_guard<std::mutex> lock(state.mutex);
        state.engine.seed(static_cast<unsigned long long>(seed));
        return Value::makeEmpty();
    };
    reg["random_choice"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
    {
        expectArgCount("random_choice", args, 1, loc);
        const auto &items = expectList("random_choice", args, 0, loc).items;
        if (items.empty())
            throw ValueError("random_choice() cannot pick from an empty list", loc);
        std::uniform_int_distribution<size_t> dist(0, items.size() - 1);
        size_t index;
        {
            auto &state = detail::randomState();
            std::lock_guard<std::mutex> lock(state.mutex);
            index = dist(state.engine);
        }
        return items[index];
    };
    reg["random_shuffle"] = [](std::vector<Value> &args, const SourceLocation &loc) -> Value
    {
        expectArgCount("random_shuffle", args, 1, loc);
        auto out = std::make_shared<ValueList>();
        out->items = expectList("random_shuffle", args, 0, loc).items;
        {
            auto &state = detail::randomState();
            std::lock_guard<std::mutex> lock(state.mutex);
            std::shuffle(out->items.begin(), out->items.end(), state.engine);
        }
        return Value::makeList(std::move(out));
    };
}
}
