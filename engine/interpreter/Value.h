#pragma once

#include "../common/Limits.h"
#include "../common/StrData.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <unordered_set>
#include <utility>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace cuff
{

    class Value;
    struct ValueList;
    class ValueMap;
    struct MatchResult;

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
    // Strings are immutable and shared (copying a Value never copies text);
    // List and Map are reference types held via shared_ptr, so assigning or
    // passing one aliases the same storage. Number/Boolean/Empty are plain
    // values.
    //
    // Adding a new value kind: add an entry to ValueType, add the storage
    // alternative + make*/as*/is* helpers below, then extend the switches in
    // truthy()/appendDisplay()/equalsImpl() at the bottom of this file.
    // =========================================================================
    class Value
    {
    public:
        using StrPtr = std::shared_ptr<const StrData>;
        using Storage = std::variant<
            std::monostate,
            double,
            StrPtr,
            bool,
            std::shared_ptr<ValueList>,
            std::shared_ptr<ValueMap>,
            std::shared_ptr<MatchResult>>;

        Value() : storage_(std::monostate{}) {}

        static Value makeEmpty() { return Value(); }
        static Value makeNumber(double d)
        {
            Value v;
            v.storage_.emplace<1>(d);
            return v;
        }
        static Value makeStr(std::string s)
        {
            Value v;
            v.storage_.emplace<2>(std::make_shared<const StrData>(std::move(s)));
            return v;
        }
        static Value makeStr(StrPtr p)
        {
            Value v;
            v.storage_.emplace<2>(std::move(p));
            return v;
        }
        static Value makeChar(unsigned char c)
        {
            if (c < 128)
                return makeStr(asciiTable()[c]);
            return makeStr(std::string(1, static_cast<char>(c)));
        }
        static Value makeBool(bool b)
        {
            Value v;
            v.storage_.emplace<3>(b);
            return v;
        }
        static Value makeList(std::shared_ptr<ValueList> l)
        {
            Value v;
            v.storage_.emplace<4>(std::move(l));
            return v;
        }
        static Value makeMap(std::shared_ptr<ValueMap> m)
        {
            Value v;
            v.storage_.emplace<5>(std::move(m));
            return v;
        }
        static Value makeMatch(std::shared_ptr<MatchResult> m)
        {
            Value v;
            v.storage_.emplace<6>(std::move(m));
            return v;
        }

        ValueType type() const { return static_cast<ValueType>(storage_.index()); }

        bool isEmpty() const { return storage_.index() == 0; }
        bool isNumber() const { return storage_.index() == 1; }
        bool isStr() const { return storage_.index() == 2; }
        bool isBool() const { return storage_.index() == 3; }
        bool isList() const { return storage_.index() == 4; }
        bool isMap() const { return storage_.index() == 5; }
        bool isMatch() const { return storage_.index() == 6; }

        double asNumber() const { return std::get<1>(storage_); }
        const std::string &asStr() const { return std::get<2>(storage_)->text(); }
        const StrData &asStrData() const { return *std::get<2>(storage_); }
        const StrPtr &asStrPtr() const { return std::get<2>(storage_); }
        bool asBool() const { return std::get<3>(storage_); }
        const std::shared_ptr<ValueList> &asList() const { return std::get<4>(storage_); }
        const std::shared_ptr<ValueMap> &asMap() const { return std::get<5>(storage_); }
        const std::shared_ptr<MatchResult> &asMatch() const { return std::get<6>(storage_); }

        // Moves the current contents out, leaving this Value empty.
        Value takeOut()
        {
            Value out;
            out.storage_.swap(storage_);
            return out;
        }

        bool truthy() const;

        std::string toDisplayString() const
        {
            std::string out;
            appendDisplay(out);
            return out;
        }
        void appendDisplay(std::string &out) const;

        // Structural equality (exact, case-sensitive) — the semantics of `is`.
        // Safe on arbitrarily deep or circular structures; see equalsImpl.
        bool strictEquals(const Value &other) const;

    private:
        Storage storage_;

        static const std::vector<StrPtr> &asciiTable()
        {
            static const std::vector<StrPtr> table = []
            {
                std::vector<StrPtr> t;
                t.reserve(128);
                for (int i = 0; i < 128; ++i)
                    t.push_back(std::make_shared<const StrData>(std::string(1, static_cast<char>(i))));
                return t;
            }();
            return table;
        }
    };

    struct ValueList
    {
        std::vector<Value> items;
        // Set once, at the point a `constant list` declaration freezes a
        // fresh copy of its value (see Interpreter::execDeclaration). Checked
        // wherever list *contents* can be mutated (add/remove/index-assign);
        // reassigning the *variable* that names a constant is a separate,
        // pre-existing check in Environment/execChange. Because this lives on
        // the value itself rather than any one variable, it also protects an
        // alias or a function argument that ends up pointing at the same
        // frozen list — a plain `list` variable can never set this to true.
        bool isConstant = false;
        ~ValueList();
    };

    // Insertion-ordered string-keyed map (Python dict / JS object semantics).
    class ValueMap
    {
    public:
        ~ValueMap();

        void set(const std::string &key, Value v)
        {
            auto it = index_.find(key);
            if (it != index_.end())
            {
                values_[it->second] = std::move(v);
                return;
            }
            index_.emplace(key, values_.size());
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
            for (size_t i = idx; i < order_.size(); ++i)
                index_[order_[i]] = i;
            return true;
        }

        size_t size() const { return values_.size(); }
        const std::vector<std::string> &keys() const { return order_; }
        const std::vector<Value> &values() const { return values_; }

        void reserve(size_t n)
        {
            order_.reserve(n);
            values_.reserve(n);
            index_.reserve(n);
        }

        void takeContainersInto(std::vector<Value> &out)
        {
            for (auto &v : values_)
                if (v.isList() || v.isMap())
                    out.push_back(v.takeOut());
        }

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

    // Destroying a deeply nested list/map through shared_ptr destructors would
    // recurse once per level. Nested containers are unhooked into a worklist
    // and released one at a time, so teardown depth stays constant.
    inline void dismantleValues(std::vector<Value> &items)
    {
        size_t i = 0;
        for (; i < items.size(); ++i)
            if (items[i].isList() || items[i].isMap())
                break;
        if (i == items.size())
            return;

        std::vector<Value> pending;
        for (; i < items.size(); ++i)
            if (items[i].isList() || items[i].isMap())
                pending.push_back(items[i].takeOut());

        while (!pending.empty())
        {
            Value v = std::move(pending.back());
            pending.pop_back();
            if (v.isList())
            {
                auto &sp = v.asList();
                if (sp && sp.use_count() == 1)
                    for (auto &c : sp->items)
                        if (c.isList() || c.isMap())
                            pending.push_back(c.takeOut());
            }
            else if (v.isMap())
            {
                auto &sp = v.asMap();
                if (sp && sp.use_count() == 1)
                    sp->takeContainersInto(pending);
            }
        }
    }

    inline ValueList::~ValueList() { dismantleValues(items); }
    inline ValueMap::~ValueMap() { dismantleValues(values_); }

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

    inline void appendCuffNumber(std::string &out, double d)
    {
        if (std::isnan(d))
        {
            out += "NaN";
            return;
        }
        if (std::isinf(d))
        {
            out += d > 0 ? "Infinity" : "-Infinity";
            return;
        }
        char buf[40];
        int len;
        if (std::fabs(d) < 1e15 && d == std::floor(d))
            len = std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(d));
        else
            len = std::snprintf(buf, sizeof buf, "%.15g", d);
        out.append(buf, static_cast<size_t>(len));
    }

    inline std::string formatCuffNumber(double d)
    {
        std::string s;
        appendCuffNumber(s, d);
        return s;
    }

    namespace detail
    {

        struct DisplayState
        {
            std::string out;
            std::vector<const void *> active;

            bool isActive(const void *p) const
            {
                return std::find(active.begin(), active.end(), p) != active.end();
            }
            bool full() const { return out.size() > limits::kMaxStringBytes; }
        };

        inline void displayNested(const Value &v, DisplayState &st);

        inline void displayList(const ValueList &list, DisplayState &st)
        {
            if (st.isActive(&list) || st.active.size() >= static_cast<size_t>(limits::kMaxValueDepth))
            {
                st.out += "[...]";
                return;
            }
            st.active.push_back(&list);
            st.out += '[';
            for (size_t i = 0; i < list.items.size(); ++i)
            {
                if (i)
                    st.out += ", ";
                displayNested(list.items[i], st);
                if (st.full())
                {
                    st.out += "...";
                    break;
                }
            }
            st.out += ']';
            st.active.pop_back();
        }

        inline void displayMap(const ValueMap &map, DisplayState &st)
        {
            if (st.isActive(&map) || st.active.size() >= static_cast<size_t>(limits::kMaxValueDepth))
            {
                st.out += "{...}";
                return;
            }
            st.active.push_back(&map);
            st.out += '{';
            const auto &ks = map.keys();
            const auto &vs = map.values();
            for (size_t i = 0; i < ks.size(); ++i)
            {
                if (i)
                    st.out += ", ";
                st.out += '"';
                st.out += ks[i];
                st.out += "\": ";
                displayNested(vs[i], st);
                if (st.full())
                {
                    st.out += "...";
                    break;
                }
            }
            st.out += '}';
            st.active.pop_back();
        }

        inline void displayNested(const Value &v, DisplayState &st)
        {
            switch (v.type())
            {
            case ValueType::Str:
                st.out += '"';
                st.out += v.asStr();
                st.out += '"';
                return;
            case ValueType::List:
                displayList(*v.asList(), st);
                return;
            case ValueType::Map:
                displayMap(*v.asMap(), st);
                return;
            default:
                v.appendDisplay(st.out);
                return;
            }
        }

        inline bool isContainer(const Value &v) { return v.isList() || v.isMap(); }

        inline bool scalarEquals(const Value &a, const Value &b)
        {
            switch (a.type())
            {
            case ValueType::Empty:
                return true;
            case ValueType::Number:
                return a.asNumber() == b.asNumber();
            case ValueType::Str:
                return &a.asStrData() == &b.asStrData() || a.asStr() == b.asStr();
            case ValueType::Boolean:
                return a.asBool() == b.asBool();
            case ValueType::Match:
                return a.asMatch()->matched == b.asMatch()->matched &&
                       a.asMatch()->wholeMatch == b.asMatch()->wholeMatch;
            default:
                return false;
            }
        }

        struct PairHash
        {
            size_t operator()(const std::pair<const void *, const void *> &p) const
            {
                return std::hash<const void *>()(p.first) * 1099511628211ULL ^ std::hash<const void *>()(p.second);
            }
        };

        // Iterative (no native recursion, so nesting depth is unbounded). After a
        // large number of container visits, already-compared pairs are skipped,
        // which makes circular structures terminate: two cyclic structures with
        // the same shape compare equal, and shared sub-structure is compared once.
        inline bool equalsImpl(const Value &a, const Value &b)
        {
            if (a.type() != b.type())
                return false;
            if (!isContainer(a))
                return scalarEquals(a, b);

            constexpr size_t kTrackAfter = 100000;
            std::vector<std::pair<const Value *, const Value *>> work;
            work.emplace_back(&a, &b);
            std::unordered_set<std::pair<const void *, const void *>, PairHash> seen;
            size_t visits = 0;

            auto queueOrCompare = [&](const Value &x, const Value &y) -> bool
            {
                if (x.type() != y.type())
                    return false;
                if (isContainer(x))
                {
                    work.emplace_back(&x, &y);
                    return true;
                }
                return scalarEquals(x, y);
            };

            while (!work.empty())
            {
                const Value *x = work.back().first;
                const Value *y = work.back().second;
                work.pop_back();

                const void *px = x->isList() ? static_cast<const void *>(x->asList().get()) : static_cast<const void *>(x->asMap().get());
                const void *py = y->isList() ? static_cast<const void *>(y->asList().get()) : static_cast<const void *>(y->asMap().get());
                if (px == py)
                    continue;
                if (++visits > kTrackAfter && !seen.emplace(px, py).second)
                    continue;

                if (x->isList())
                {
                    const auto &ia = x->asList()->items;
                    const auto &ib = y->asList()->items;
                    if (ia.size() != ib.size())
                        return false;
                    for (size_t i = 0; i < ia.size(); ++i)
                        if (!queueOrCompare(ia[i], ib[i]))
                            return false;
                }
                else
                {
                    const ValueMap &ma = *x->asMap();
                    const ValueMap &mb = *y->asMap();
                    if (ma.size() != mb.size())
                        return false;
                    const auto &ks = ma.keys();
                    const auto &vs = ma.values();
                    for (size_t i = 0; i < ks.size(); ++i)
                    {
                        const Value *bv = mb.get(ks[i]);
                        if (!bv || !queueOrCompare(vs[i], *bv))
                            return false;
                    }
                }
            }
            return true;
        }

    } // namespace detail

    inline void Value::appendDisplay(std::string &out) const
    {
        switch (type())
        {
        case ValueType::Empty:
            out += "empty";
            return;
        case ValueType::Number:
            appendCuffNumber(out, asNumber());
            return;
        case ValueType::Str:
            out += asStr();
            return;
        case ValueType::Boolean:
            out += asBool() ? "true" : "false";
            return;
        case ValueType::List:
        {
            detail::DisplayState st;
            detail::displayList(*asList(), st);
            out += st.out;
            return;
        }
        case ValueType::Map:
        {
            detail::DisplayState st;
            detail::displayMap(*asMap(), st);
            out += st.out;
            return;
        }
        case ValueType::Match:
            if (!asMatch()->matched)
            {
                out += "empty";
                return;
            }
            out += "<match \"";
            out += asMatch()->wholeMatch;
            out += "\">";
            return;
        }
    }

    inline bool Value::strictEquals(const Value &other) const
    {
        return detail::equalsImpl(*this, other);
    }

} // namespace cuff
