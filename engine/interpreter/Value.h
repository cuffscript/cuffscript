#pragma once

#include "../common/Attributes.h"
#include "../common/Limits.h"
#include "../common/StrData.h"
#include "../regex/PatternData.h"
#include "ClassModel.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <unordered_set>
#include <utility>
#include <string>
#include <unordered_map>
#include <vector>

namespace cuff
{

    class Value;
    struct ValueList;
    class ValueMap;
    struct MatchResult;
    struct ObjectData;

    enum class ValueType
    {
        Empty,
        Number,
        Str,
        Boolean,
        List,
        Map,
        Match,
        Pattern,
        Object
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
        case ValueType::Pattern:
            return "pattern";
        case ValueType::Object:
            return "object";
        }
        return "unknown";
    }

    // A value is a tag and an eight-byte payload or a counted pointer. The tag is the ValueType, so type() is a
    // cast. Numbers, booleans and `empty` hold no pointer, so copying, moving and dropping them is a few
    // instructions with no call: that is the point of writing this by hand instead of using std::variant, whose
    // copy, move and destructor are a visit per operation on every temporary an expression makes.
    class Value
    {
    public:
        using StrPtr = std::shared_ptr<const StrData>;

        Value() noexcept : tag_(ValueType::Empty) {}

        Value(const Value &o) : tag_(o.tag_)
        {
            if (isPlain(tag_))
                u_.bits = o.u_.bits;
            else
                copyCounted(o);
        }

        Value(Value &&o) noexcept : tag_(ValueType::Empty) { takeFrom(o); }

        Value &operator=(const Value &o)
        {
            if (this != &o)
            {
                Value copy(o);
                *this = std::move(copy);
            }
            return *this;
        }

        Value &operator=(Value &&o) noexcept
        {
            if (this == &o)
                return *this;
            if (isPlain(tag_))
            {
                takeFrom(o);
            }
            else
            {
                // Take `o` first and let the old content go last: releasing it may free a structure that holds `o`.
                Value old(std::move(*this));
                takeFrom(o);
            }
            return *this;
        }

        ~Value()
        {
            if (!isPlain(tag_))
                releaseCounted();
        }

        static Value makeEmpty() { return Value(); }
        static Value makeNumber(double d)
        {
            Value v;
            v.tag_ = ValueType::Number;
            v.u_.num = d;
            return v;
        }
        static Value makeStr(std::string s)
        {
            return makeStr(std::make_shared<const StrData>(std::move(s)));
        }
        static Value makeStr(StrPtr p)
        {
            Value v;
            new (&v.u_.str) StrPtr(std::move(p));
            v.tag_ = ValueType::Str;
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
            v.tag_ = ValueType::Boolean;
            v.u_.bits = b ? 1 : 0;
            return v;
        }
        static Value makeList(std::shared_ptr<ValueList> l)
        {
            Value v;
            new (&v.u_.list) std::shared_ptr<ValueList>(std::move(l));
            v.tag_ = ValueType::List;
            return v;
        }
        static Value makeMap(std::shared_ptr<ValueMap> m)
        {
            Value v;
            new (&v.u_.map) std::shared_ptr<ValueMap>(std::move(m));
            v.tag_ = ValueType::Map;
            return v;
        }
        static Value makeMatch(std::shared_ptr<MatchResult> m)
        {
            Value v;
            new (&v.u_.match) std::shared_ptr<MatchResult>(std::move(m));
            v.tag_ = ValueType::Match;
            return v;
        }
        static Value makePattern(std::shared_ptr<const PatternData> p)
        {
            Value v;
            new (&v.u_.pattern) std::shared_ptr<const PatternData>(std::move(p));
            v.tag_ = ValueType::Pattern;
            return v;
        }
        static Value makeObject(std::shared_ptr<ObjectData> o)
        {
            Value v;
            new (&v.u_.object) std::shared_ptr<ObjectData>(std::move(o));
            v.tag_ = ValueType::Object;
            return v;
        }
        ValueType type() const { return tag_; }

        bool isEmpty() const { return tag_ == ValueType::Empty; }
        bool isNumber() const { return tag_ == ValueType::Number; }
        bool isStr() const { return tag_ == ValueType::Str; }
        bool isBool() const { return tag_ == ValueType::Boolean; }
        bool isList() const { return tag_ == ValueType::List; }
        bool isMap() const { return tag_ == ValueType::Map; }
        bool isMatch() const { return tag_ == ValueType::Match; }
        bool isPattern() const { return tag_ == ValueType::Pattern; }
        bool isObject() const { return tag_ == ValueType::Object; }

        // The accessors trust the caller to have asked for the right type, as every call site does; a builds
        // without NDEBUG (the sanitizer runs) checks it.
        double asNumber() const { expect(ValueType::Number); return u_.num; }
        const std::string &asStr() const { expect(ValueType::Str); return u_.str->text(); }
        const StrData &asStrData() const { expect(ValueType::Str); return *u_.str; }
        const StrPtr &asStrPtr() const { expect(ValueType::Str); return u_.str; }
        bool asBool() const { expect(ValueType::Boolean); return u_.bits != 0; }
        const std::shared_ptr<ValueList> &asList() const { expect(ValueType::List); return u_.list; }
        const std::shared_ptr<ValueMap> &asMap() const { expect(ValueType::Map); return u_.map; }
        const std::shared_ptr<MatchResult> &asMatch() const { expect(ValueType::Match); return u_.match; }
        const std::shared_ptr<const PatternData> &asPattern() const { expect(ValueType::Pattern); return u_.pattern; }
        const std::shared_ptr<ObjectData> &asObject() const { expect(ValueType::Object); return u_.object; }

        Value takeOut()
        {
            return std::move(*this);
        }

        // The three below are for the machine's registers and a program's variables: places that no structure
        // owns, so the old content can be let go before the new one arrives, which the assignment operators cannot
        // assume. `o` may be this very value (`x = x` compiles to a register stored into itself), and then nothing
        // changes; the test for that is only made where it matters, off the path that numbers take.

        // Moves `o` here and leaves it empty.
        CUFF_ALWAYS_INLINE void take(Value &o) noexcept
        {
            if (isPlain(o.tag_))
            {
                const ValueType tag = o.tag_;
                const uint64_t bits = o.u_.bits;
                if (!isPlain(tag_))
                    releaseCounted();
                o.tag_ = ValueType::Empty;
                tag_ = tag;
                u_.bits = bits;
                return;
            }
            if (this == &o)
                return;
            if (!isPlain(tag_))
                releaseCounted();
            takeFrom(o);
        }

        // Copies `o` here.
        CUFF_ALWAYS_INLINE void put(const Value &o) noexcept
        {
            if (!isPlain(tag_))
            {
                if (this == &o)
                    return;
                releaseCounted();
            }
            tag_ = o.tag_;
            if (isPlain(tag_))
                u_.bits = o.u_.bits;
            else
                copyCounted(o);
        }

        // Copies `o` here when the old content may be what holds `o`: an item of the list, or a field of the object,
        // that this register is about to stop referring to.
        CUFF_ALWAYS_INLINE void putOwned(const Value &o) noexcept
        {
            if (isPlain(o.tag_))
            {
                const ValueType tag = o.tag_;
                const uint64_t bits = o.u_.bits;
                if (!isPlain(tag_))
                    releaseCounted();
                tag_ = tag;
                u_.bits = bits;
                return;
            }
            Value copy(o);
            take(copy);
        }

        // Writes a plain value over this one. The machine does this on every arithmetic step, where a temporary
        // and an assignment would cost a move each.
        void setNumber(double d) noexcept
        {
            if (!isPlain(tag_))
                releaseCounted();
            tag_ = ValueType::Number;
            u_.num = d;
        }
        void setBool(bool b) noexcept
        {
            if (!isPlain(tag_))
                releaseCounted();
            tag_ = ValueType::Boolean;
            u_.bits = b ? 1 : 0;
        }
        void clear() noexcept
        {
            if (!isPlain(tag_))
                releaseCounted();
            tag_ = ValueType::Empty;
        }

        bool truthy() const;

        std::string toDisplayString() const
        {
            std::string out;
            appendDisplay(out);
            return out;
        }
        void appendDisplay(std::string &out) const;

        bool strictEquals(const Value &other) const;

    private:
        // Empty, number and boolean hold their payload in `bits`; every other type holds a counted pointer.
        static constexpr bool isPlain(ValueType t)
        {
            return t == ValueType::Empty || t == ValueType::Number || t == ValueType::Boolean;
        }

        // Makes this value what `o` is and leaves `o` empty. This value must hold nothing that needs releasing.
        void takeFrom(Value &o) noexcept
        {
            tag_ = o.tag_;
            if (isPlain(tag_))
                u_.bits = o.u_.bits;
            else
                moveCounted(o);
            o.tag_ = ValueType::Empty;
        }

        void expect([[maybe_unused]] ValueType t) const
        {
#ifndef NDEBUG
            assert(tag_ == t && "a Value was read as a type it does not hold");
#endif
        }

        union Payload
        {
            uint64_t bits;
            double num;
            StrPtr str;
            std::shared_ptr<ValueList> list;
            std::shared_ptr<ValueMap> map;
            std::shared_ptr<MatchResult> match;
            std::shared_ptr<const PatternData> pattern;
            std::shared_ptr<ObjectData> object;
            Payload() : bits(0) {}
            ~Payload() {}
        };

        // The counted types. Out of line, because the plain types never come here.
        CUFF_NOINLINE void copyCounted(const Value &o)
        {
            switch (tag_)
            {
            case ValueType::Str: new (&u_.str) StrPtr(o.u_.str); break;
            case ValueType::List: new (&u_.list) std::shared_ptr<ValueList>(o.u_.list); break;
            case ValueType::Map: new (&u_.map) std::shared_ptr<ValueMap>(o.u_.map); break;
            case ValueType::Match: new (&u_.match) std::shared_ptr<MatchResult>(o.u_.match); break;
            case ValueType::Pattern: new (&u_.pattern) std::shared_ptr<const PatternData>(o.u_.pattern); break;
            case ValueType::Object: new (&u_.object) std::shared_ptr<ObjectData>(o.u_.object); break;
            default: break;
            }
        }

        CUFF_NOINLINE void moveCounted(Value &o) noexcept
        {
            switch (tag_)
            {
            case ValueType::Str: new (&u_.str) StrPtr(std::move(o.u_.str)); o.u_.str.~StrPtr(); break;
            case ValueType::List: new (&u_.list) std::shared_ptr<ValueList>(std::move(o.u_.list)); o.u_.list.~shared_ptr(); break;
            case ValueType::Map: new (&u_.map) std::shared_ptr<ValueMap>(std::move(o.u_.map)); o.u_.map.~shared_ptr(); break;
            case ValueType::Match: new (&u_.match) std::shared_ptr<MatchResult>(std::move(o.u_.match)); o.u_.match.~shared_ptr(); break;
            case ValueType::Pattern:
                new (&u_.pattern) std::shared_ptr<const PatternData>(std::move(o.u_.pattern));
                o.u_.pattern.~shared_ptr();
                break;
            case ValueType::Object: new (&u_.object) std::shared_ptr<ObjectData>(std::move(o.u_.object)); o.u_.object.~shared_ptr(); break;
            default: break;
            }
        }

        CUFF_NOINLINE void releaseCounted() noexcept
        {
            switch (tag_)
            {
            case ValueType::Str: u_.str.~StrPtr(); break;
            case ValueType::List: u_.list.~shared_ptr(); break;
            case ValueType::Map: u_.map.~shared_ptr(); break;
            case ValueType::Match: u_.match.~shared_ptr(); break;
            case ValueType::Pattern: u_.pattern.~shared_ptr(); break;
            case ValueType::Object: u_.object.~shared_ptr(); break;
            default: break;
            }
        }

        ValueType tag_;
        Payload u_;

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
        // Set once, when a `constant list` declaration freezes its fresh copy.
        bool isConstant = false;
        ~ValueList();
    };

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

        bool remove(const std::string &key)
        {
            auto it = index_.find(key);
            if (it == index_.end())
                return false;
            size_t idx = it->second;
            index_.erase(it);
            order_.erase(order_.begin() + static_cast<long>(idx));
            values_.erase(values_.begin() + static_cast<long>(idx));
            size_t later = order_.size() - idx;
            if (later * 8 < index_.size())
            {
                for (size_t i = idx; i < order_.size(); ++i)
                    index_[order_[i]] = i;
            }
            else
            {
                // Many keys moved: walking the index is cheaper than re-hashing each one.
                for (auto &entry : index_)
                    if (entry.second > idx)
                        --entry.second;
            }
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
                if (v.isList() || v.isMap() || v.isObject())
                    out.push_back(v.takeOut());
        }

    private:
        std::vector<std::string> order_;
        std::vector<Value> values_;
        std::unordered_map<std::string, size_t> index_;
    };

    // An instance of a class. The fields are in the order of ClassInfo::fields. `origin` is set on an Error that
    // stands for an error the engine raised, so that `fail` can raise that error again unchanged.
    struct ObjectData
    {
        std::shared_ptr<const ClassInfo> cls;
        std::vector<Value> fields;
        std::shared_ptr<const void> origin;
        bool constructing = false; // true while its `init` runs: only then may a `constant` field be given its value
        ~ObjectData();
    };

    // Lets the interpreter turn an object into text when a value is printed or put in an f-string, since that can
    // mean running the object's `to_str` method. The interpreter that is running installs itself for the thread.
    struct ObjectDisplayHook
    {
        virtual void display(const Value &object, std::string &out) = 0;

    protected:
        ~ObjectDisplayHook() = default;
    };

    inline ObjectDisplayHook *&activeObjectDisplay()
    {
        static thread_local ObjectDisplayHook *hook = nullptr;
        return hook;
    }

    struct MatchResult
    {
        bool matched = false;
        std::string wholeMatch;
        // 1-based positional captures: positional[0] == group 1, etc.
        std::vector<std::string> positional;
        std::unordered_map<std::string, std::string> named;
    };

    // Destroying a deeply nested list/map/object chain would recurse once per level through shared_ptr destructors.
    inline bool holdsOtherValues(const Value &v) { return v.isList() || v.isMap() || v.isObject(); }

    inline void dismantleValues(std::vector<Value> &items)
    {
        size_t i = 0;
        for (; i < items.size(); ++i)
            if (holdsOtherValues(items[i]))
                break;
        if (i == items.size())
            return;

        std::vector<Value> pending;
        for (; i < items.size(); ++i)
            if (holdsOtherValues(items[i]))
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
                        if (holdsOtherValues(c))
                            pending.push_back(c.takeOut());
            }
            else if (v.isMap())
            {
                auto &sp = v.asMap();
                if (sp && sp.use_count() == 1)
                    sp->takeContainersInto(pending);
            }
            else if (v.isObject())
            {
                auto &sp = v.asObject();
                if (sp && sp.use_count() == 1)
                    for (auto &c : sp->fields)
                        if (holdsOtherValues(c))
                            pending.push_back(c.takeOut());
            }
        }
    }

    inline ValueList::~ValueList() { dismantleValues(items); }
    inline ValueMap::~ValueMap() { dismantleValues(values_); }
    inline ObjectData::~ObjectData() { dismantleValues(fields); }

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
        case ValueType::Pattern:
        case ValueType::Object:
            return true;
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
        if (std::fabs(d) < 1e15 && d == std::floor(d))
        {
            // A whole number, which is what most numbers a program shows are: its digits, written from the last one
            // back. The text is what "%lld" gives, without the cost of going through printf.
            const long long n = static_cast<long long>(d);
            unsigned long long u = n < 0 ? 0ULL - static_cast<unsigned long long>(n) : static_cast<unsigned long long>(n);
            char *const end = buf + sizeof buf;
            char *p = end;
            do
            {
                *--p = static_cast<char>('0' + u % 10);
                u /= 10;
            } while (u != 0);
            if (n < 0)
                *--p = '-';
            out.append(p, static_cast<size_t>(end - p));
            return;
        }
        const int len = std::snprintf(buf, sizeof buf, "%.15g", d);
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
            case ValueType::Pattern:
                return a.asPattern() == b.asPattern() || a.asPattern()->sameAs(*b.asPattern());
            case ValueType::Object:
                return a.asObject() == b.asObject();
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

        // Iterative comparison avoids call-stack growth for deeply nested or cyclic values.
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

    }

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
        case ValueType::Pattern:
            out += asPattern()->display();
            return;
        case ValueType::Object:
            if (ObjectDisplayHook *hook = activeObjectDisplay())
            {
                hook->display(*this, out);
                return;
            }
            out += '<';
            out += asObject()->cls->name;
            out += '>';
            return;
        }
    }

    inline bool Value::strictEquals(const Value &other) const
    {
        return detail::equalsImpl(*this, other);
    }

}
