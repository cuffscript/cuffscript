#pragma once

#include "../common/Attributes.h"
#include "../common/Limits.h"
#include "../common/Text.h"
#include "../regex/RegexEngine.h"
#include "ClassModel.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
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

    // Where a list, a map or an object is entered in the books of the engine that made it (see CycleHeap, below).
    // Six bytes aligned to two, so that it fits where each of the three had padding: a list and an object are no
    // larger for it.
    struct CycleLink
    {
        uint16_t slotLow = 0, slotHigh = 0; // its place in the books, plus one; 0 when no engine keeps it
        uint16_t heap = 0;                  // which engine's books

        uint32_t slot() const { return slotLow | (static_cast<uint32_t>(slotHigh) << 16); }
        void setSlot(uint32_t s)
        {
            slotLow = static_cast<uint16_t>(s);
            slotHigh = static_cast<uint16_t>(s >> 16);
        }

        enum Kind : unsigned
        {
            List,
            Map,
            Object
        };
        void enter(void *container, Kind kind);
        void leave() noexcept;
    };

    // The three kinds that need no counting come first, so that telling them from the rest is one comparison.
    enum class ValueType
    {
        Empty,
        Number,
        Boolean,
        Str,
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
            return makeStr(std::make_shared<StrData>(std::move(s))); // not a const object: binarySlow may add to it
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
            return t <= ValueType::Boolean;
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
                    t.push_back(std::make_shared<StrData>(std::string(1, static_cast<char>(i))));
                return t;
            }();
            return table;
        }
    };

    struct ValueList
    {
        std::vector<Value> items;
        CycleLink cycles;
        // Set once, when a `constant list` declaration freezes its fresh copy.
        bool isConstant = false;

        ValueList() { cycles.enter(this, CycleLink::List); }
        ValueList(const ValueList &o) : items(o.items), isConstant(o.isConstant) { cycles.enter(this, CycleLink::List); }
        ValueList &operator=(const ValueList &o)
        {
            items = o.items;
            isConstant = o.isConstant;
            return *this;
        }
        ~ValueList();
    };

    // A map keeps its keys in the order they were first set. The keys and the values are two arrays; beside them is
    // a table that says where in those arrays a key is, by the key's hash. The table holds positions only (four
    // bytes each), not the keys again, and a map of a few keys has no table at all: its keys are looked through.
    class ValueMap
    {
    public:
        ValueMap() { cycles.enter(this, CycleLink::Map); }
        ValueMap(const ValueMap &o) : order_(o.order_), values_(o.values_), hashes_(o.hashes_), slots_(o.slots_)
        {
            cycles.enter(this, CycleLink::Map);
        }
        ValueMap &operator=(const ValueMap &o)
        {
            if (this != &o)
            {
                ValueMap copy(o);
                order_.swap(copy.order_);
                values_.swap(copy.values_);
                hashes_.swap(copy.hashes_);
                slots_.swap(copy.slots_);
            }
            return *this;
        }
        ~ValueMap();

        CycleLink cycles;

        CUFF_NOINLINE void set(const std::string &key, Value v)
        {
            // The key is hashed once: for the search, for its place in the table and for every later rebuild of it.
            uint32_t h = slots_.empty() ? 0 : hashOf(key);
            const size_t at = slots_.empty() ? findInOrder(key) : findHashed(key, h);
            if (at != kNone)
            {
                values_[at] = std::move(v);
                return;
            }
            // Room is made first, so that a failure to get memory leaves the map as it was.
            const size_t n = order_.size();
            if (n + 1 >= kNone)
                throw std::length_error("map too large");
            if (order_.capacity() == n)
                order_.reserve(n < 4 ? 4 : n * 2);
            if (values_.capacity() == n)
                values_.reserve(n < 4 ? 4 : n * 2);
            if (n + 1 > kLookedThrough && (n + 1) * 4 > slots_.size() * 3)
            {
                if (slots_.empty())
                    h = hashOf(key);
                index(slots_.empty() ? 32 : slots_.size() * 2);
            }
            if (!slots_.empty() && hashes_.capacity() == n)
                hashes_.reserve(n < 4 ? 4 : n * 2);
            order_.push_back(key);
            values_.push_back(std::move(v));
            if (!slots_.empty())
            {
                hashes_.push_back(h);
                place(n);
            }
        }

        bool has(const std::string &key) const { return find(key) != kNone; }

        const Value *get(const std::string &key) const
        {
            const size_t at = find(key);
            return at == kNone ? nullptr : &values_[at];
        }

        bool remove(const std::string &key)
        {
            const size_t at = find(key);
            if (at == kNone)
                return false;
            order_.erase(order_.begin() + static_cast<std::ptrdiff_t>(at));
            values_.erase(values_.begin() + static_cast<std::ptrdiff_t>(at));
            if (!hashes_.empty())
                hashes_.erase(hashes_.begin() + static_cast<std::ptrdiff_t>(at));
            // Every later key moved up by one, so the table is written again (in the memory it has).
            if (order_.size() <= kLookedThrough)
            {
                std::vector<uint32_t>().swap(slots_);
                std::vector<uint32_t>().swap(hashes_);
            }
            else if (!slots_.empty())
                index(slots_.size());
            return true;
        }

        size_t size() const { return values_.size(); }
        const std::vector<std::string> &keys() const { return order_; }
        const std::vector<Value> &values() const { return values_; }

        void reserve(size_t n)
        {
            order_.reserve(n);
            values_.reserve(n);
            if (n > kLookedThrough)
            {
                size_t slots = 32;
                while (n * 4 > slots * 3)
                    slots *= 2;
                if (slots > slots_.size())
                    index(slots);
                hashes_.reserve(n);
            }
        }

        // For dismantleValues only: the map is being destroyed, and its values are taken from it one by one.
        std::vector<Value> &valuesForTeardown() { return values_; }

        // For CycleHeap only: empties the map, handing its values over. `out` has room for them.
        void emptyInto(std::vector<Value> &out) noexcept
        {
            for (Value &v : values_)
                out.push_back(std::move(v));
            values_.clear();
            order_.clear();
            std::vector<uint32_t>().swap(hashes_);
            std::vector<uint32_t>().swap(slots_);
        }

    private:
        static constexpr size_t kNone = static_cast<uint32_t>(-1);
        static constexpr size_t kLookedThrough = 8;

        static uint32_t hashOf(const std::string &key) { return static_cast<uint32_t>(std::hash<std::string>()(key)); }

        CUFF_NOINLINE size_t find(const std::string &key) const { return slots_.empty() ? findInOrder(key) : findHashed(key, hashOf(key)); }

        size_t findInOrder(const std::string &key) const
        {
            for (size_t i = 0, n = order_.size(); i < n; ++i)
                if (order_[i] == key)
                    return i;
            return kNone;
        }

        size_t findHashed(const std::string &key, uint32_t h) const
        {
            const size_t mask = slots_.size() - 1;
            for (size_t at = h & mask;; at = (at + 1) & mask)
            {
                const uint32_t slot = slots_[at];
                if (slot == 0)
                    return kNone;
                if (hashes_[slot - 1] == h && order_[slot - 1] == key)
                    return slot - 1;
            }
        }

        // Records in the table where key number `i` is.
        void place(size_t i)
        {
            const size_t mask = slots_.size() - 1;
            size_t at = hashes_[i] & mask;
            while (slots_[at] != 0)
                at = (at + 1) & mask;
            slots_[at] = static_cast<uint32_t>(i + 1);
        }

        // Writes the table again, with `slots` places (a power of two, more than the keys there are). The keys of a map
        // that had no table yet are hashed now; the others keep theirs. Nothing changes if memory runs out on the way.
        void index(size_t slots)
        {
            std::vector<uint32_t> hashes;
            if (hashes_.size() != order_.size())
            {
                hashes.reserve(order_.capacity());
                for (const std::string &key : order_)
                    hashes.push_back(hashOf(key));
            }
            std::vector<uint32_t> table(slots, 0);
            if (!hashes.empty())
                hashes_.swap(hashes);
            slots_.swap(table);
            for (size_t i = 0, n = order_.size(); i < n; ++i)
                place(i);
        }

        std::vector<std::string> order_;
        std::vector<Value> values_;
        std::vector<uint32_t> hashes_; // the hash of each key, in the order of the keys, once the map has a table
        std::vector<uint32_t> slots_;  // 0 for a free place, otherwise a key's position plus one
    };

    // An instance of a class. The fields are in the order of ClassInfo::fields. `origin` is what the engine keeps
    // behind an object of one of its own classes: on an Error that stands for an error the engine raised, that
    // error, so that `fail` can raise it again unchanged; on a Task, the task.
    struct ObjectData
    {
        std::shared_ptr<const ClassInfo> cls;
        std::vector<Value> fields;
        std::shared_ptr<void> origin;
        // A value that `origin` keeps for this object, outside its fields: a task's result. The search for cycles
        // looks through it as through a field, so that a task whose result refers back to the object that keeps its
        // Task is freed like any cycle. Only the object made with the task has it, never a copy: two objects that
        // both reported it would count one reference twice.
        Value *held = nullptr;
        CycleLink cycles;
        bool constructing = false; // true while its `init` runs: only then may a `constant` field be given its value

        ObjectData() { cycles.enter(this, CycleLink::Object); }
        ObjectData(const ObjectData &o) : cls(o.cls), fields(o.fields), origin(o.origin), constructing(o.constructing)
        {
            cycles.enter(this, CycleLink::Object);
        }
        ObjectData &operator=(const ObjectData &o)
        {
            cls = o.cls;
            fields = o.fields;
            origin = o.origin;
            held = nullptr;
            constructing = o.constructing;
            return *this;
        }
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

    // Something a library function waits for: a moment in time, or a socket becoming ready. A function that would
    // otherwise hold the whole program up for that long hands one of these to the machine (activeWaitHook) and
    // returns. The machine sets the task that called aside and runs the others; when the wait is over, the call's
    // value is what finish() gives.
    struct Wait
    {
        using Clock = std::chrono::steady_clock;
        virtual ~Wait() = default;
        // Does what can be done without blocking. True when there is nothing left to wait for.
        virtual bool advance() = 0;
        // The value of the call, or the error it ends with (thrown). Called once, after advance() returned true.
        virtual Value finish(const SourceLocation &loc) = 0;
        // The time by which advance() must be tried again even if nothing happened on the socket.
        virtual Clock::time_point wakeAt() const = 0;
        // True for a wait that opens a socket: the machine keeps the number of those that are open at once bounded,
        // and does not call advance() for the first time before there is room.
        virtual bool usesSocket() const { return false; }
        // The socket advance() waits on (-1 when it waits for the time only), whether it waits to write to it, and
        // how to wait on such sockets.
        virtual long long socket() const { return -1; }
        virtual bool wantsWrite() const { return false; }
        virtual SocketPoller poller() const { return nullptr; }
    };

    // How a library function reaches the machine that is running, to hand it a Wait.
    struct WaitHook
    {
        virtual void waitFor(std::unique_ptr<Wait> wait) = 0;

    protected:
        ~WaitHook() = default;
    };

    inline WaitHook *&activeWaitHook()
    {
        static thread_local WaitHook *hook = nullptr;
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

    // Destroying a deeply nested list/map/object chain would recurse once per level through shared_ptr destructors,
    // so a container takes what it holds apart before its members are destroyed. This runs in destructors, often
    // when memory has just run out, so it allocates nothing: the way back up from a container is kept in the slot
    // its child was taken from (the last one, which is then dropped), as in the pointer-reversing tree walks.
    inline bool holdsOtherValues(const Value &v) { return v.isList() || v.isMap() || v.isObject(); }

    // True for a list, map or object that this value alone refers to: destroying the value destroys it.
    inline bool solelyHeld(const Value &v)
    {
        if (v.isList())
            return v.asList().use_count() == 1;
        if (v.isMap())
            return v.asMap().use_count() == 1;
        return v.isObject() && v.asObject().use_count() == 1;
    }

    inline std::vector<Value> &heldValues(const Value &v)
    {
        if (v.isList())
            return v.asList()->items;
        if (v.isMap())
            return v.asMap()->valuesForTeardown();
        return v.asObject()->fields;
    }

    inline void dismantleValues(std::vector<Value> &items) noexcept
    {
        size_t first = 0;
        for (; first < items.size(); ++first)
            if (solelyHeld(items[first]))
                break;
        if (first == items.size())
            return;

        std::vector<Value> *cur = &items;
        Value owner; // the container `cur` belongs to; empty while that is the one being destroyed
        Value above; // the container `owner` was found in; empty when that is the one being destroyed
        for (;;)
        {
            while (!cur->empty() && !solelyHeld(cur->back()))
                cur->pop_back();
            if (!cur->empty())
            {
                // Down into the last value.
                Value child = cur->back().takeOut();
                cur->back() = std::move(above);
                above = std::move(owner);
                owner = std::move(child);
                cur = &heldValues(owner);
            }
            else if (owner.isEmpty())
            {
                return;
            }
            else if (above.isEmpty())
            {
                // Up to where this began. `owner` holds nothing now, so letting it go nests no further.
                owner = Value();
                cur = &items;
                cur->pop_back();
            }
            else
            {
                Value parent = std::move(above);
                std::vector<Value> &held = heldValues(parent);
                above = held.back().takeOut();
                held.pop_back();
                owner = std::move(parent);
                cur = &held;
            }
        }
    }

    inline ValueList::~ValueList()
    {
        cycles.leave();
        dismantleValues(items);
    }
    inline ValueMap::~ValueMap()
    {
        cycles.leave();
        dismantleValues(values_);
    }
    inline ObjectData::~ObjectData()
    {
        cycles.leave();
        dismantleValues(fields);
    }

    // ---- cycles ---------------------------------------------------------------------------------------------------
    //
    // Values are counted: a list, map or object is freed when the last value that refers to it goes. Counting alone
    // never frees a cycle (two objects that hold each other, a list that contains itself), because each member keeps
    // the next one's count above zero. A CycleHeap finds such groups and takes them apart.
    //
    // It keeps books of the lists, maps and objects made while its engine runs. To collect, it counts for each of
    // them how many of the references to it come from inside the books (from the values the others hold). One whose
    // count of references is higher than that is held from outside as well: by a variable, a register, a task, the
    // host. Those, and everything that can be reached from them, are in use. What is left is referred to only by
    // itself, and is emptied; the counts then fall to zero in the ordinary way.
    //
    // It errs on one side only. A reference it cannot see (in a structure it keeps no books of) counts as one from
    // outside, so the worst it can do is leave a cycle unfreed. It never has to know where the engine's own
    // references are, which is why it can run in the middle of a program.
    //
    // The books of one engine are written by one thread at a time, as the engine is: values an engine made are let
    // go on the thread that is using that engine.
    class CycleHeap
    {
    public:
        CycleHeap()
        {
            std::lock_guard<std::mutex> lock(tableLock());
            for (uint16_t id = 1; id < kHeaps; ++id)
                if (!table()[id])
                {
                    table()[id] = this;
                    id_ = id;
                    break;
                }
            // With every number taken (thousands of engines alive at once) this one keeps no books.
        }

        CycleHeap(const CycleHeap &) = delete;
        CycleHeap &operator=(const CycleHeap &) = delete;

        // The engine's own references are gone when this runs (it is the engine's first member, so the last to be
        // destroyed): what the books still hold is in use by the host or is a cycle. The cycles are freed, and the
        // rest is struck from the books, since those go away here.
        ~CycleHeap()
        {
            collect();
            for (uintptr_t entry : slots_)
                if ((entry & kKindMask) != kFree)
                    linkOf(entry)->setSlot(0);
            if (id_)
            {
                std::lock_guard<std::mutex> lock(tableLock());
                table()[id_] = nullptr;
            }
        }

        // The heap that new lists, maps and objects are entered in: that of the engine running on this thread.
        static CycleHeap *&active()
        {
            static thread_local CycleHeap *heap = nullptr;
            return heap;
        }

        // A heap that has no number keeps no books: nothing is entered while it is the active one.
        struct Scope
        {
            CycleHeap *previous;
            explicit Scope(CycleHeap *heap) : previous(active()) { active() = heap && heap->id_ ? heap : nullptr; }
            ~Scope() { active() = previous; }
        };

        // Makes the count at `steps` run out at its next use once enough has been made for a collection to be worth
        // it (the engine counts the steps of a program there, and collects when it finds the count was borrowed).
        void ringAt(uint64_t *steps) { alarm_ = steps; }
        bool rang() const { return armed_; }
        // Gives the count its own value back.
        uint64_t silence()
        {
            armed_ = false;
            return saved_;
        }
        void setEnabled(bool on)
        {
            enabled_ = on;
            untilDue_ = on ? limit_ : SIZE_MAX;
        }

        size_t tracked() const { return live_; }
        bool due() const { return due_; }

        void enter(void *container, CycleLink::Kind kind, CycleLink &link)
        {
            uint32_t slot = free_;
            if (slot != 0)
            {
                free_ = static_cast<uint32_t>(slots_[slot - 1] >> 2);
            }
            else
            {
                if (CUFF_UNLIKELY(slots_.size() >= UINT32_MAX - 1))
                    return;
                slots_.push_back(0);
                slot = static_cast<uint32_t>(slots_.size());
            }
            slots_[slot - 1] = reinterpret_cast<uintptr_t>(container) | kind;
            link.setSlot(slot);
            link.heap = id_;
            ++live_;
            if (CUFF_UNLIKELY(--untilDue_ == 0))
                becomeDue();
        }

        static void leave(CycleLink &link) noexcept
        {
            CycleHeap *heap = table()[link.heap];
            if (!heap)
                return;
            const uint32_t slot = link.slot();
            heap->slots_[slot - 1] = (static_cast<uintptr_t>(heap->free_) << 2) | kFree;
            heap->free_ = slot;
            --heap->live_;
            link.setSlot(0);
        }

        // Frees the cycles nothing else refers to; how many lists, maps and objects that was. It gives up, having
        // changed nothing, when there is no memory for its own notes.
        size_t collect() noexcept
        {
            due_ = false;
            if (!id_ || live_ == 0 || collecting_)
            {
                untilDue_ = enabled_ ? limit_ : SIZE_MAX;
                return 0;
            }
            collecting_ = true;
            size_t freed = 0;
            const size_t before = live_;
            try
            {
                freed = sweep();
            }
            catch (...)
            {
            }
            collecting_ = false;
            // A program that makes no cycles is looked at less and less often; one that does, as often as it grows.
            if (freed * 8 >= before)
                patience_ = 1;
            else if (patience_ < kMaxPatience)
                patience_ *= 2;
            limit_ = std::max<size_t>(kFirstLimit, live_ * patience_);
#ifdef CUFF_CYCLE_STRESS
            limit_ = 1 + live_ / 32;
#endif
            untilDue_ = enabled_ ? limit_ : SIZE_MAX;
            return freed;
        }

    private:
        static constexpr size_t kHeaps = 4096;
        static constexpr uintptr_t kKindMask = 3, kFree = 3;
        // A program makes this many lists, maps and objects before its engine first looks for cycles among them.
        // (A build with CUFF_CYCLE_STRESS looks whenever one was made since it last looked, while fewer than 32 are
        // alive: nearly at every step. With more, it waits for one new value for every 32 alive, so a test that
        // keeps a hundred thousand values still ends. The tests use it to find a value that is freed too early.)
        static constexpr size_t kFirstLimit = 10000;
        static constexpr size_t kMaxPatience = 4;

        CUFF_COLD void becomeDue()
        {
            due_ = true;
            untilDue_ = SIZE_MAX;
            ringIfDue();
        }

    public:
        // A collection that came due while no run was going on (or as one ended in an error) is made at the next step.
        void ringIfDue()
        {
            if (due_ && enabled_ && alarm_ && !armed_)
            {
                armed_ = true;
                saved_ = *alarm_;
                *alarm_ = 1;
            }
        }

    private:
        static CycleHeap **table()
        {
            static CycleHeap *heaps[kHeaps] = {};
            return heaps;
        }

        static std::mutex &tableLock()
        {
            static std::mutex lock;
            return lock;
        }

        static CycleLink *linkOf(uintptr_t entry)
        {
            void *at = reinterpret_cast<void *>(entry & ~kKindMask);
            switch (entry & kKindMask)
            {
            case CycleLink::List:
                return &static_cast<ValueList *>(at)->cycles;
            case CycleLink::Map:
                return &static_cast<ValueMap *>(at)->cycles;
            default:
                return &static_cast<ObjectData *>(at)->cycles;
            }
        }

        // Calls `visit` with every value a recorded list, map or object holds.
        template <typename Visit>
        static void forEachHeld(uintptr_t entry, Visit &&visit)
        {
            for (const Value &v : valuesOf(entry))
                visit(v);
            if ((entry & kKindMask) == CycleLink::Object)
                if (const Value *held = static_cast<ObjectData *>(reinterpret_cast<void *>(entry & ~kKindMask))->held)
                    visit(*held);
        }

        static std::vector<Value> &valuesOf(uintptr_t entry)
        {
            void *at = reinterpret_cast<void *>(entry & ~kKindMask);
            switch (entry & kKindMask)
            {
            case CycleLink::List:
                return static_cast<ValueList *>(at)->items;
            case CycleLink::Map:
                return static_cast<ValueMap *>(at)->valuesForTeardown();
            default:
                return static_cast<ObjectData *>(at)->fields;
            }
        }

        // The place in these books of what `v` refers to, plus one; 0 when it is not in them. `count` is set to the
        // number of references there are to it.
        uint32_t slotOf(const Value &v, long &count) const
        {
            const CycleLink *link;
            if (v.isList())
            {
                link = &v.asList()->cycles;
                count = v.asList().use_count();
            }
            else if (v.isMap())
            {
                link = &v.asMap()->cycles;
                count = v.asMap().use_count();
            }
            else if (v.isObject())
            {
                link = &v.asObject()->cycles;
                count = v.asObject().use_count();
            }
            else
                return 0;
            return link->heap == id_ ? link->slot() : 0;
        }

        size_t sweep()
        {
            const size_t n = slots_.size();
            // For each entry: how many references to it were found inside the books, and how many there are.
            std::vector<uint32_t> inside(n, 0);
            std::vector<long> total(n, 0);
            for (size_t s = 0; s < n; ++s)
            {
                if ((slots_[s] & kKindMask) == kFree)
                    continue;
                forEachHeld(slots_[s], [&](const Value &v)
                            {
                                long count = 0;
                                if (const uint32_t slot = slotOf(v, count))
                                {
                                    ++inside[slot - 1];
                                    total[slot - 1] = count;
                                }
                            });
            }

            // In use: held from outside the books, and all that can be reached from there.
            std::vector<uint8_t> inUse(n, 0);
            std::vector<uint32_t> pending;
            for (size_t s = 0; s < n; ++s)
            {
                if ((slots_[s] & kKindMask) == kFree || inUse[s])
                    continue;
                if (inside[s] != 0 && static_cast<long>(inside[s]) >= total[s])
                    continue;
                inUse[s] = 1;
                pending.push_back(static_cast<uint32_t>(s));
                while (!pending.empty())
                {
                    const uint32_t at = pending.back();
                    pending.pop_back();
                    forEachHeld(slots_[at], [&](const Value &v)
                                {
                                    long count = 0;
                                    const uint32_t slot = slotOf(v, count);
                                    if (slot && !inUse[slot - 1])
                                    {
                                        inUse[slot - 1] = 1;
                                        pending.push_back(slot - 1);
                                    }
                                });
                }
            }

            // The rest is held only by itself. Its values are taken out together, so that none of it is destroyed
            // while another part still points to it, and are let go at the end.
            size_t values = 0, freed = 0;
            for (size_t s = 0; s < n; ++s)
                if ((slots_[s] & kKindMask) != kFree && !inUse[s])
                {
                    values += valuesOf(slots_[s]).size() + 1;
                    ++freed;
                }
            if (freed == 0)
                return 0;
            std::vector<Value> doomed;
            doomed.reserve(values);
            for (size_t s = 0; s < n; ++s)
            {
                const uintptr_t entry = slots_[s];
                if ((entry & kKindMask) == kFree || inUse[s])
                    continue;
                void *at = reinterpret_cast<void *>(entry & ~kKindMask);
                switch (entry & kKindMask)
                {
                case CycleLink::List:
                {
                    std::vector<Value> &items = static_cast<ValueList *>(at)->items;
                    for (Value &v : items)
                        doomed.push_back(std::move(v));
                    items.clear();
                    break;
                }
                case CycleLink::Map:
                    static_cast<ValueMap *>(at)->emptyInto(doomed);
                    break;
                default:
                {
                    // An object keeps its fields, empty: code that reaches one finds it by its place.
                    ObjectData &o = *static_cast<ObjectData *>(at);
                    for (Value &v : o.fields)
                        doomed.push_back(std::move(v));
                    if (o.held)
                        doomed.push_back(std::move(*o.held));
                    break;
                }
                }
            }
            return freed;
        }

        std::vector<uintptr_t> slots_; // a list, map or object and which it is; or the next free place
        uint32_t free_ = 0;            // the first free place, plus one
        uint16_t id_ = 0;
        bool enabled_ = true;
        bool armed_ = false;
        bool collecting_ = false;
        bool due_ = false;
        size_t live_ = 0;
#ifdef CUFF_CYCLE_STRESS
        size_t limit_ = 1;
#else
        size_t limit_ = kFirstLimit;
#endif
        size_t untilDue_ = limit_; // what may still be entered before the next collection is due
        size_t patience_ = 1;
        uint64_t *alarm_ = nullptr;
        uint64_t saved_ = 0;
    };

    inline void CycleLink::enter(void *container, Kind kind)
    {
        if (CycleHeap *heap = CycleHeap::active())
            heap->enter(container, kind, *this);
    }

    inline void CycleLink::leave() noexcept
    {
        if (slotLow | slotHigh)
            CycleHeap::leave(*this);
    }

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

    // The text of a number as the language writes it, into `buf` (40 bytes is always enough). Returns its length.
    inline size_t cuffNumberText(char *buf, double d)
    {
        if (std::isnan(d))
        {
            std::memcpy(buf, "NaN", 3);
            return 3;
        }
        if (std::isinf(d))
        {
            if (d > 0)
            {
                std::memcpy(buf, "Infinity", 8);
                return 8;
            }
            std::memcpy(buf, "-Infinity", 9);
            return 9;
        }
        if (std::fabs(d) < 1e15)
        {
            // A whole number, which is what most numbers a program shows are: what "%lld" gives, without the cost of
            // going through printf. The round trip through long long tells a whole number (|d| < 1e15 fits) without a
            // call to floor, and the digits go two at a time.
            const long long n = static_cast<long long>(d);
            if (static_cast<double>(n) == d)
            {
                static constexpr char kPairs[] = "00010203040506070809101112131415161718192021222324252627282930313233343536373839"
                                                 "40414243444546474849505152535455565758596061626364656667686970717273747576777879"
                                                 "8081828384858687888990919293949596979899";
                unsigned long long u = n < 0 ? 0ULL - static_cast<unsigned long long>(n) : static_cast<unsigned long long>(n);
                size_t digits = 1;
                for (unsigned long long p = 10; digits < 16 && u >= p; p *= 10)
                    ++digits;
                const size_t len = digits + (n < 0 ? 1 : 0);
                char *q = buf + len;
                while (u >= 100)
                {
                    const unsigned r = static_cast<unsigned>(u % 100) * 2;
                    u /= 100;
                    *--q = kPairs[r + 1];
                    *--q = kPairs[r];
                }
                if (u >= 10)
                {
                    const unsigned r = static_cast<unsigned>(u) * 2;
                    *--q = kPairs[r + 1];
                    *--q = kPairs[r];
                }
                else
                    *--q = static_cast<char>('0' + u);
                if (n < 0)
                    *--q = '-';
                return len;
            }
        }
        return numtext::format15g(buf, 40, d);
    }

    inline void appendCuffNumber(std::string &out, double d)
    {
        char buf[40];
        out.append(buf, cuffNumberText(buf, d));
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
