#pragma once

#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <unordered_map>
#include <sstream>
#include <cmath>
#include <algorithm>

namespace cuff
{

    class Value;
    struct ValueList;  // ordered list of Value — defined after Value below
    class ValueMap;    // insertion-ordered string-keyed map of Value
    struct MatchResult; // regex capture result (positional 1-based + named)

    enum class ValueType
    {
        Empty,
        Number,
        Str,
        Boolean,
        List,
        Map,
        Match
    };

    inline std::string valueTypeName(ValueType t)
    {
        switch (t)
        {
        case ValueType::Empty:
            return "empty";
        case ValueType::Number:
            return "number";
        case ValueType::Str:
            return "str";
        case ValueType::Boolean:
            return "boolean";
        case ValueType::List:
            return "list";
        case ValueType::Map:
            return "map";
        case ValueType::Match:
            return "match";
        }
        return "unknown";
    }

    // =========================================================================
    // Value — the interpreter's runtime value representation.
    //
    // List and Map are reference types (held via shared_ptr): assigning a list
    // to another variable, or passing it as a function argument, aliases the
    // same underlying storage — this matches the "add/remove/change mutate in
    // place" behavior the language spec describes for collections, and is the
    // same choice most dynamic languages this spec is inspired by (Python,
    // JS) make. Every other kind (Number/Str/Boolean/Empty/Match) is a plain
    // value copied by value.
    //
    // Adding a new value kind: add an entry to ValueType, add the storage
    // alternative + make*/as*/is* helpers below, then extend the switch in
    // truthy()/toDisplayString()/strictEquals() at the bottom of this file.
    // =========================================================================
    class Value
    {
    public:
        using Storage = std::variant<
            std::monostate,               // Empty
            double,                       // Number
            std::string,                  // Str
            bool,                         // Boolean
            std::shared_ptr<ValueList>,   // List
            std::shared_ptr<ValueMap>,    // Map
            std::shared_ptr<MatchResult>> // Match
            ;

        Value() : storage_(std::monostate{}) {}

        static Value makeEmpty() { return Value(); }
        static Value makeNumber(double d)
        {
            Value v;
            v.storage_ = d;
            return v;
        }
        static Value makeStr(std::string s)
        {
            Value v;
            v.storage_ = std::move(s);
            return v;
        }
        static Value makeBool(bool b)
        {
            Value v;
            v.storage_ = b;
            return v;
        }
        static Value makeList(std::shared_ptr<ValueList> l)
        {
            Value v;
            v.storage_ = std::move(l);
            return v;
        }
        static Value makeMap(std::shared_ptr<ValueMap> m)
        {
            Value v;
            v.storage_ = std::move(m);
            return v;
        }
        static Value makeMatch(std::shared_ptr<MatchResult> m)
        {
            Value v;
            v.storage_ = std::move(m);
            return v;
        }

        ValueType type() const
        {
            switch (storage_.index())
            {
            case 0:
                return ValueType::Empty;
            case 1:
                return ValueType::Number;
            case 2:
                return ValueType::Str;
            case 3:
                return ValueType::Boolean;
            case 4:
                return ValueType::List;
            case 5:
                return ValueType::Map;
            case 6:
                return ValueType::Match;
            }
            return ValueType::Empty;
        }

        bool isEmpty() const { return storage_.index() == 0; }
        bool isNumber() const { return storage_.index() == 1; }
        bool isStr() const { return storage_.index() == 2; }
        bool isBool() const { return storage_.index() == 3; }
        bool isList() const { return storage_.index() == 4; }
        bool isMap() const { return storage_.index() == 5; }
        bool isMatch() const { return storage_.index() == 6; }

        double asNumber() const { return std::get<double>(storage_); }
        const std::string &asStr() const { return std::get<std::string>(storage_); }
        bool asBool() const { return std::get<bool>(storage_); }
        const std::shared_ptr<ValueList> &asList() const { return std::get<std::shared_ptr<ValueList>>(storage_); }
        const std::shared_ptr<ValueMap> &asMap() const { return std::get<std::shared_ptr<ValueMap>>(storage_); }
        const std::shared_ptr<MatchResult> &asMatch() const { return std::get<std::shared_ptr<MatchResult>>(storage_); }

        // Truthiness used by `!`, and defensively by `if`/`loop while` in case
        // a condition expression isn't already boolean-valued.
        bool truthy() const;

        // Human-readable rendering — used by print(), f-string interpolation,
        // and Str+X coercions.
        std::string toDisplayString() const;

        // Structural equality (exact, case-sensitive) — the semantics of `is`.
        bool strictEquals(const Value &other) const;

    private:
        Storage storage_;
    };

    struct ValueList
    {
        std::vector<Value> items;
    };

    // Insertion-ordered string-keyed map (Python dict / JS object semantics).
    class ValueMap
    {
    public:
        void set(const std::string &key, Value v)
        {
            auto it = index_.find(key);
            if (it != index_.end())
            {
                values_[it->second] = std::move(v);
                return;
            }
            index_[key] = values_.size();
            order_.push_back(key);
            values_.push_back(std::move(v));
        }

        bool has(const std::string &key) const { return index_.count(key) > 0; }

        const Value *get(const std::string &key) const
        {
            auto it = index_.find(key);
            if (it == index_.end())
                return nullptr;
            return &values_[it->second];
        }

        Value *getMutable(const std::string &key)
        {
            auto it = index_.find(key);
            if (it == index_.end())
                return nullptr;
            return &values_[it->second];
        }

        bool remove(const std::string &key)
        {
            auto it = index_.find(key);
            if (it == index_.end())
                return false;
            size_t idx = it->second;
            index_.erase(it);
            order_.erase(order_.begin() + static_cast<long>(idx));
            values_.erase(values_.begin() + static_cast<long>(idx));
            for (auto &kv : index_)
                if (kv.second > idx)
                    kv.second--;
            return true;
        }

        size_t size() const { return values_.size(); }
        const std::vector<std::string> &keys() const { return order_; }
        const std::vector<Value> &values() const { return values_; }

    private:
        std::vector<std::string> order_;
        std::vector<Value> values_;
        std::unordered_map<std::string, size_t> index_;
    };

    struct MatchResult
    {
        bool matched = false;
        std::string wholeMatch;
        // 1-based positional captures: positional[0] == group 1, etc.
        std::vector<std::string> positional;
        std::unordered_map<std::string, std::string> named;
    };

    // ---- Out-of-line Value methods (need ValueList/ValueMap/MatchResult complete) ----

    inline bool Value::truthy() const
    {
        switch (type())
        {
        case ValueType::Empty:
            return false;
        case ValueType::Number:
            return asNumber() != 0.0;
        case ValueType::Str:
            return !asStr().empty();
        case ValueType::Boolean:
            return asBool();
        case ValueType::List:
            return !asList()->items.empty();
        case ValueType::Map:
            return asMap()->size() > 0;
        case ValueType::Match:
            return asMatch()->matched;
        }
        return false;
    }

    inline std::string formatCuffNumber(double d)
    {
        if (std::isnan(d))
            return "NaN";
        if (std::isinf(d))
            return d > 0 ? "Infinity" : "-Infinity";
        // Whole numbers print without a trailing ".0" (25, not 25.0) — matches
        // every numeric example in the language spec.
        if (d == static_cast<double>(static_cast<long long>(d)) && std::fabs(d) < 1e15)
        {
            std::ostringstream os;
            os << static_cast<long long>(d);
            return os.str();
        }
        std::ostringstream os;
        os.precision(15);
        os << d;
        return os.str();
    }

    inline std::string Value::toDisplayString() const
    {
        switch (type())
        {
        case ValueType::Empty:
            return "empty";
        case ValueType::Number:
            return formatCuffNumber(asNumber());
        case ValueType::Str:
            return asStr();
        case ValueType::Boolean:
            return asBool() ? "true" : "false";
        case ValueType::List:
        {
            std::ostringstream os;
            os << "[";
            const auto &items = asList()->items;
            for (size_t i = 0; i < items.size(); ++i)
            {
                if (i)
                    os << ", ";
                if (items[i].isStr())
                    os << "\"" << items[i].asStr() << "\"";
                else
                    os << items[i].toDisplayString();
            }
            os << "]";
            return os.str();
        }
        case ValueType::Map:
        {
            std::ostringstream os;
            os << "{";
            const auto &m = asMap();
            const auto &ks = m->keys();
            for (size_t i = 0; i < ks.size(); ++i)
            {
                if (i)
                    os << ", ";
                const Value *v = m->get(ks[i]);
                os << "\"" << ks[i] << "\": ";
                if (v->isStr())
                    os << "\"" << v->asStr() << "\"";
                else
                    os << v->toDisplayString();
            }
            os << "}";
            return os.str();
        }
        case ValueType::Match:
        {
            if (!asMatch()->matched)
                return "empty";
            std::ostringstream os;
            os << "<match \"" << asMatch()->wholeMatch << "\">";
            return os.str();
        }
        }
        return "";
    }

    inline bool Value::strictEquals(const Value &other) const
    {
        if (type() != other.type())
            return false;
        switch (type())
        {
        case ValueType::Empty:
            return true;
        case ValueType::Number:
            return asNumber() == other.asNumber();
        case ValueType::Str:
            return asStr() == other.asStr();
        case ValueType::Boolean:
            return asBool() == other.asBool();
        case ValueType::List:
        {
            const auto &a = asList()->items;
            const auto &b = other.asList()->items;
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (!a[i].strictEquals(b[i]))
                    return false;
            return true;
        }
        case ValueType::Map:
        {
            const auto &a = asMap();
            const auto &b = other.asMap();
            if (a->size() != b->size())
                return false;
            for (auto &k : a->keys())
            {
                const Value *bv = b->get(k);
                if (!bv || !a->get(k)->strictEquals(*bv))
                    return false;
            }
            return true;
        }
        case ValueType::Match:
            return asMatch()->matched == other.asMatch()->matched &&
                   asMatch()->wholeMatch == other.asMatch()->wholeMatch;
        }
        return false;
    }

} // namespace cuff
