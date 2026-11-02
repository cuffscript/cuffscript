#pragma once

#include "../interpreter/Value.h"
#include <vector>

namespace cuff
{

    // The kinds of value a built-in method can be called on.
    enum Receiver : unsigned
    {
        RecvStr = 1u << 0,
        RecvList = 1u << 1,
        RecvMap = 1u << 2,
        RecvNumber = 1u << 3,
        RecvBoolean = 1u << 4,
        RecvEmpty = 1u << 5,
        RecvMatch = 1u << 6,
        RecvPattern = 1u << 7,
        RecvObject = 1u << 8,
        RecvAny = (1u << 9) - 1,
    };

    inline unsigned receiverBit(ValueType t)
    {
        switch (t)
        {
        case ValueType::Str:
            return RecvStr;
        case ValueType::List:
            return RecvList;
        case ValueType::Map:
            return RecvMap;
        case ValueType::Number:
            return RecvNumber;
        case ValueType::Boolean:
            return RecvBoolean;
        case ValueType::Empty:
            return RecvEmpty;
        case ValueType::Match:
            return RecvMatch;
        case ValueType::Pattern:
            return RecvPattern;
        case ValueType::Object:
            return RecvObject;
        }
        return 0;
    }

    // `value.method(args)` on a built-in value runs `function(value, args)`. `library` is the `use DLC:` library the
    // function comes from, or empty when it is always available. This is the one list of built-in methods: the
    // function index in the documentation and the "undefined method" hints are produced from it.
    struct MethodEntry
    {
        const char *method;
        const char *function;
        unsigned receivers;
        const char *library;
    };

    inline const std::vector<MethodEntry> &methodTable()
    {
        static const std::vector<MethodEntry> table = {
            // always available
            {"find", "find", RecvStr, ""},
            {"find_all", "find_all", RecvStr, ""},
            {"match", "match", RecvStr, ""},
            {"replace", "replace", RecvStr, ""},
            {"replace_all", "replace_all", RecvStr, ""},
            {"split", "split", RecvStr, ""},
            {"count", "count", RecvStr, ""},
            {"copy", "copy", RecvList | RecvMap | RecvObject, ""},
            {"to_str", "to_str", RecvAny, ""},
            {"to_number", "to_number", RecvAny, ""},
            {"to_boolean", "to_boolean", RecvAny, ""},

            // DLC:string
            {"upper", "str_upper", RecvStr, "string"},
            {"lower", "str_lower", RecvStr, "string"},
            {"trim", "str_trim", RecvStr, "string"},
            {"trim_start", "str_trim_start", RecvStr, "string"},
            {"trim_end", "str_trim_end", RecvStr, "string"},
            {"starts_with", "str_starts_with", RecvStr, "string"},
            {"ends_with", "str_ends_with", RecvStr, "string"},
            {"repeat", "str_repeat", RecvStr, "string"},
            {"char_code", "str_char_code", RecvStr, "string"},
            {"length", "length", RecvStr, "string"},
            {"contains", "contains", RecvStr, "string"},
            {"index_of", "index_of", RecvStr, "string"},

            // DLC:list
            {"length", "length", RecvList, "list"},
            {"contains", "contains", RecvList, "list"},
            {"index_of", "index_of", RecvList, "list"},
            {"sort", "list_sort", RecvList, "list"},
            {"reverse", "list_reverse", RecvList, "list"},
            {"join", "list_join", RecvList, "list"},
            {"unique", "list_unique", RecvList, "list"},
            {"sum", "list_sum", RecvList, "list"},
            {"average", "list_average", RecvList, "list"},
            {"flatten", "list_flatten", RecvList, "list"},

            // DLC:map
            {"length", "length", RecvMap, "map"},
            {"contains", "contains", RecvMap, "map"},
            {"keys", "map_keys", RecvMap, "map"},
            {"values", "map_values", RecvMap, "map"},
            {"has_key", "map_has_key", RecvMap, "map"},
            {"entries", "map_entries", RecvMap, "map"},
            {"merge", "map_merge", RecvMap, "map"},

            // DLC:math
            {"sqrt", "math_sqrt", RecvNumber, "math"},
            {"abs", "math_abs", RecvNumber, "math"},
            {"pow", "math_pow", RecvNumber, "math"},
            {"round", "math_round", RecvNumber, "math"},
            {"floor", "math_floor", RecvNumber, "math"},
            {"ceil", "math_ceil", RecvNumber, "math"},
            {"trunc", "math_trunc", RecvNumber, "math"},
            {"sign", "math_sign", RecvNumber, "math"},
            {"clamp", "math_clamp", RecvNumber, "math"},
            {"mod", "math_mod", RecvNumber, "math"},
            {"log", "math_log", RecvNumber, "math"},
            {"log10", "math_log10", RecvNumber, "math"},
            {"log2", "math_log2", RecvNumber, "math"},
            {"exp", "math_exp", RecvNumber, "math"},
            {"sin", "math_sin", RecvNumber, "math"},
            {"cos", "math_cos", RecvNumber, "math"},
            {"tan", "math_tan", RecvNumber, "math"},
            {"asin", "math_asin", RecvNumber, "math"},
            {"acos", "math_acos", RecvNumber, "math"},
            {"atan", "math_atan", RecvNumber, "math"},
            {"atan2", "math_atan2", RecvNumber, "math"},

            // DLC:json
            {"to_json", "to_json", RecvAny, "json"},
            {"from_json", "from_json", RecvStr, "json"},
        };
        return table;
    }

}
