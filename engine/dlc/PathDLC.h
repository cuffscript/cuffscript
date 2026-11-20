#pragma once

#include "DLCCommon.h"

// DLC:path: file paths as text. Nothing here looks at the file system (that is DLC:filesystem), so every function
// gives the same answer on every machine. The separator is `/`, as in the paths a program writes for `use` and for
// DLC:filesystem; a backslash is an ordinary character. A path is absolute when it starts with `/`, or with a drive
// letter, a colon and `/` (`C:/data`).

namespace cuff
{

    namespace pathtext
    {

        // The part of `p` that is its root: "/", "C:/", or nothing.
        inline std::string rootOf(const std::string &p)
        {
            if (!p.empty() && p[0] == '/')
                return "/";
            if (p.size() >= 3 && ascii::isLetter(static_cast<unsigned char>(p[0])) && p[1] == ':' && p[2] == '/')
                return p.substr(0, 3);
            return std::string();
        }

        CUFF_NOINLINE inline std::vector<std::string> partsOf(const std::string &p, size_t from)
        {
            std::vector<std::string> parts;
            size_t i = from;
            while (i <= p.size())
            {
                size_t j = p.find('/', i);
                if (j == std::string::npos)
                    j = p.size();
                if (j > i)
                    parts.push_back(p.substr(i, j - i));
                i = j + 1;
            }
            return parts;
        }

        // `.` parts are dropped, and `..` takes the part before it away. A relative path keeps the `..` it cannot
        // resolve; at a root there is nothing above, so `..` there is dropped.
        CUFF_NOINLINE inline std::string normalize(const std::string &p)
        {
            const std::string root = rootOf(p);
            std::vector<std::string> out;
            for (std::string &part : partsOf(p, root.size()))
            {
                if (part == ".")
                    continue;
                if (part == "..")
                {
                    if (!out.empty() && out.back() != "..")
                        out.pop_back();
                    else if (root.empty())
                        out.push_back(part);
                    continue;
                }
                out.push_back(std::move(part));
            }
            std::string result = root;
            for (size_t k = 0; k < out.size(); ++k)
                result += (k ? "/" : "") + out[k];
            return result.empty() ? std::string(".") : result;
        }

        // The last part, without trailing slashes; nothing for a root or an empty path.
        CUFF_NOINLINE inline std::string basename(const std::string &p)
        {
            const size_t rootSize = rootOf(p).size();
            size_t end = p.size();
            while (end > rootSize && p[end - 1] == '/')
                --end;
            if (end <= rootSize)
                return std::string();
            const size_t slash = p.rfind('/', end - 1);
            const size_t start = (slash == std::string::npos || slash < rootSize) ? rootSize : slash + 1;
            return p.substr(start, end - start);
        }

        CUFF_NOINLINE inline std::string dirname(const std::string &p)
        {
            const std::string root = rootOf(p);
            size_t end = p.size();
            while (end > root.size() && p[end - 1] == '/')
                --end;
            size_t slash = end > root.size() ? p.rfind('/', end - 1) : std::string::npos;
            if (slash == std::string::npos || slash + 1 <= root.size())
                return root.empty() ? std::string(".") : root;
            while (slash > root.size() && p[slash - 1] == '/')
                --slash;
            return p.substr(0, slash);
        }

        // Where the extension of a name begins, or npos: at its last dot, if that dot comes after a character that
        // is not a dot. So `.bashrc` and `..` have none, and `a.tar.gz` has `.gz`.
        inline size_t extensionAt(const std::string &name)
        {
            const size_t first = name.find_first_not_of('.');
            const size_t dot = name.rfind('.');
            if (first == std::string::npos || dot == std::string::npos || dot < first)
                return std::string::npos;
            return dot;
        }

    }

    inline void registerPathDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        // Plain functions, not one closure type each: std::function then needs one handler for all of them, which
        // keeps the library small.
        auto add = [&reg](const char *name, Value (*fn)(std::vector<Value> &, const SourceLocation &)) { reg[name] = fn; };

        using namespace pathtext;

        // Joins the parts with `/`. A part that is absolute starts the path again, as it would in a shell.
        add("path_join", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            if (args.empty())
                throw ArgumentError("path_join() expects at least 1 argument(s), got 0", loc);
            std::string out;
            for (size_t k = 0; k < args.size(); ++k)
            {
                const std::string &part = expectStr("path_join", args, k, loc);
                if (!rootOf(part).empty() || out.empty())
                    out = part;
                else if (!part.empty())
                    out += (out.back() == '/' ? "" : "/") + part;
                ensureStringSize(out.size(), loc);
            }
            return Value::makeStr(std::move(out));
        });
        add("path_normalize", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("path_normalize", args, 1, loc);
            return Value::makeStr(normalize(expectStr("path_normalize", args, 0, loc)));
        });
        add("path_basename", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("path_basename", args, 1, loc);
            return Value::makeStr(basename(expectStr("path_basename", args, 0, loc)));
        });
        add("path_dirname", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("path_dirname", args, 1, loc);
            return Value::makeStr(dirname(expectStr("path_dirname", args, 0, loc)));
        });
        add("path_extension", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("path_extension", args, 1, loc);
            const std::string name = basename(expectStr("path_extension", args, 0, loc));
            const size_t at = extensionAt(name);
            return Value::makeStr(at == std::string::npos ? std::string() : name.substr(at));
        });
        add("path_stem", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("path_stem", args, 1, loc);
            const std::string name = basename(expectStr("path_stem", args, 0, loc));
            const size_t at = extensionAt(name);
            return Value::makeStr(at == std::string::npos ? name : name.substr(0, at));
        });
        // The path with its extension replaced: "" removes it, and any other extension starts with a dot.
        add("path_with_extension", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("path_with_extension", args, 2, loc);
            const std::string &p = expectStr("path_with_extension", args, 0, loc);
            const std::string &ext = expectStr("path_with_extension", args, 1, loc);
            if (!ext.empty() && (ext[0] != '.' || ext.size() == 1 || ext.find('/') != std::string::npos))
                throw ValueError("path_with_extension() expects an extension such as \".txt\", or \"\" to remove it, got \"" + ext + "\"", loc);
            const std::string name = basename(p);
            if (name.empty())
                throw ValueError("path_with_extension() expects a path that ends in a name, got \"" + p + "\"", loc);
            size_t end = p.size();
            while (end > 0 && p[end - 1] == '/')
                --end;
            const size_t at = extensionAt(name);
            const size_t nameStart = end - name.size();
            std::string out = p.substr(0, at == std::string::npos ? end : nameStart + at) + ext;
            return Value::makeStr(std::move(out));
        });
        add("path_split", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("path_split", args, 1, loc);
            const std::string &p = expectStr("path_split", args, 0, loc);
            const std::string root = rootOf(p);
            auto out = std::make_shared<ValueList>();
            if (!root.empty())
                out->items.push_back(Value::makeStr(root));
            for (std::string &part : partsOf(p, root.size()))
                out->items.push_back(Value::makeStr(std::move(part)));
            return Value::makeList(std::move(out));
        });
        add("path_is_absolute", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("path_is_absolute", args, 1, loc);
            return Value::makeBool(!rootOf(expectStr("path_is_absolute", args, 0, loc)).empty());
        });
        // The path that leads from the folder `from` to `to`. Both are normalized first, and both must be absolute
        // with the same root, or both relative.
        add("path_relative", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("path_relative", args, 2, loc);
            const std::string from = normalize(expectStr("path_relative", args, 0, loc));
            const std::string to = normalize(expectStr("path_relative", args, 1, loc));
            const std::string rootFrom = rootOf(from), rootTo = rootOf(to);
            if (rootFrom != rootTo)
                throw ValueError("path_relative() expects two absolute paths with the same root, or two relative paths, got \"" + from +
                                     "\" and \"" + to + "\"",
                                 loc);
            std::vector<std::string> a = partsOf(from, rootFrom.size()), b = partsOf(to, rootTo.size());
            if (a.size() == 1 && a[0] == ".")
                a.clear();
            if (b.size() == 1 && b[0] == ".")
                b.clear();
            size_t common = 0;
            while (common < a.size() && common < b.size() && a[common] == b[common])
                ++common;
            for (size_t k = common; k < a.size(); ++k)
                if (a[k] == "..")
                    throw ValueError("path_relative() cannot tell where \"" + from + "\" is, because it leaves its starting folder", loc);
            std::string out;
            for (size_t k = common; k < a.size(); ++k)
                out += out.empty() ? ".." : "/..";
            for (size_t k = common; k < b.size(); ++k)
                out += (out.empty() ? "" : "/") + b[k];
            return Value::makeStr(out.empty() ? std::string(".") : out);
        });
    }

}
