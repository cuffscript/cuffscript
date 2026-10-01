#pragma once

#include "DLCCommon.h"

#include "../common/PathSandbox.h"
#include <filesystem>
#include <fstream>

namespace cuff
{

    // ---- DLC:filesystem ----
    // Every path is resolved relative to, and confined inside, the sandbox
    // root the host configured (CuffEngine::Options::rootDir, defaulting to
    // the running script's own directory) — the exact same root `use ... from
    // ...` module imports are confined to (Interpreter::moduleRoot_). An
    // absolute path, or a relative path that escapes the root (e.g.
    // `../../etc/passwd`), is rejected with a loud FilesystemAccessDenied
    // error rather than silently failing, since that's a script trying to
    // reach outside its sandbox — worth surfacing clearly, unlike an ordinary
    // "file not found". Once a path clears that check, ordinary OS-level
    // failures (missing file, permission denied, ...) are reported the quiet
    // way each function's own table entry promises (`empty`/`false`), not by
    // throwing — a missing file is a completely normal thing for a script to
    // check for with `file_exist()` first.
    struct FilesystemDLCOptions
    {
        bool enabled = true;
        std::filesystem::path root;
    };

    [[noreturn]] inline void throwFilesystemDisabled(const char *fn, const SourceLocation &loc)
    {
        throw ModuleError(ErrorCode::DLCFeatureUnavailable,
                          std::string("DLC:filesystem's ") + fn + "() is disabled for this run (filesystem access was turned off by the host)",
                          loc, "the host embedding this engine controls this — see CuffEngine::Options::filesystemEnabled");
    }

    [[noreturn]] inline void throwFilesystemAccessDenied(const char *fn, const std::string &rawPath, const SourceLocation &loc)
    {
        throw ModuleError(ErrorCode::FilesystemAccessDenied,
                          std::string("DLC:filesystem's ") + fn + "(\"" + rawPath + "\") reaches outside the sandboxed root",
                          loc, "use a path relative to, and inside, the script's directory (or the host's configured rootDir)");
    }

    // Resolves `rawPath` against opts.root and rejects anything that would
    // escape it. Returns the resolved (not-necessarily-existing) path.
    inline std::filesystem::path resolveSandboxedPath(const char *fn, const std::string &rawPath,
                                                       const FilesystemDLCOptions &opts, const SourceLocation &loc)
    {
        namespace fs = std::filesystem;
        fs::path rel(rawPath);
        if (rel.has_root_name() || rel.has_root_directory())
            throwFilesystemAccessDenied(fn, rawPath, loc);
        fs::path full = opts.root / rel;
        std::error_code ec;
        fs::path canon = fs::weakly_canonical(full, ec);
        if (ec)
            canon = full.lexically_normal();
        if (!isInsideRoot(canon, opts.root))
            throwFilesystemAccessDenied(fn, rawPath, loc);
        return canon;
    }

    inline void registerFilesystemDLC(std::unordered_map<std::string, NativeFn> &reg, FilesystemDLCOptions opts)
    {
        reg["file_exist"] = [opts](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("file_exist", args, 1, loc);
            if (!opts.enabled)
                throwFilesystemDisabled("file_exist", loc);
            auto resolved = resolveSandboxedPath("file_exist", expectStr("file_exist", args, 0, loc), opts, loc);
            std::error_code ec;
            bool exists = std::filesystem::is_regular_file(resolved, ec) && !ec;
            return Value::makeBool(exists);
        };

        reg["file_size"] = [opts](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("file_size", args, 1, loc);
            if (!opts.enabled)
                throwFilesystemDisabled("file_size", loc);
            auto resolved = resolveSandboxedPath("file_size", expectStr("file_size", args, 0, loc), opts, loc);
            std::error_code ec;
            auto sz = std::filesystem::file_size(resolved, ec);
            if (ec)
                return Value::makeEmpty();
            return Value::makeNumber(static_cast<double>(sz));
        };

        reg["file_read"] = [opts](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("file_read", args, 1, loc);
            if (!opts.enabled)
                throwFilesystemDisabled("file_read", loc);
            auto resolved = resolveSandboxedPath("file_read", expectStr("file_read", args, 0, loc), opts, loc);
            std::error_code ec;
            auto sz = std::filesystem::file_size(resolved, ec);
            if (ec)
                return Value::makeEmpty();
            // Checked against the file's stat'd size *before* allocating or
            // reading anything, unlike slurping the whole thing first — a
            // multi-gigabyte file on the sandboxed filesystem shouldn't be
            // able to force a multi-gigabyte allocation just to get rejected.
            if (sz > limits::kMaxStringBytes)
                throw CuffRuntimeError(ErrorCode::SizeLimitExceeded,
                                       "file_read(): file exceeds the maximum allowed string size", loc);
            std::ifstream in(resolved, std::ios::binary);
            if (!in)
                return Value::makeEmpty();
            std::string content(static_cast<size_t>(sz), '\0');
            if (sz > 0)
                in.read(&content[0], static_cast<std::streamsize>(sz));
            if (!in)
                return Value::makeEmpty();
            return Value::makeStr(std::move(content));
        };

        reg["file_readlines"] = [opts](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("file_readlines", args, 1, loc);
            if (!opts.enabled)
                throwFilesystemDisabled("file_readlines", loc);
            auto resolved = resolveSandboxedPath("file_readlines", expectStr("file_readlines", args, 0, loc), opts, loc);
            std::error_code ec;
            auto sz = std::filesystem::file_size(resolved, ec);
            if (ec)
                return Value::makeEmpty();
            if (sz > limits::kMaxStringBytes)
                throw CuffRuntimeError(ErrorCode::SizeLimitExceeded,
                                       "file_readlines(): file exceeds the maximum allowed string size", loc);
            std::ifstream in(resolved, std::ios::binary);
            if (!in)
                return Value::makeEmpty();
            auto list = std::make_shared<ValueList>();
            std::string line;
            while (std::getline(in, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back(); // tolerate CRLF line endings
                if (list->items.size() >= limits::kMaxCollectionItems)
                    throw CuffRuntimeError(ErrorCode::SizeLimitExceeded,
                                           "file_readlines(): file has too many lines", loc);
                list->items.push_back(Value::makeStr(line));
            }
            if (!in.eof())
                return Value::makeEmpty();
            return Value::makeList(list);
        };

        reg["file_write"] = [opts](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("file_write", args, 2, loc);
            if (!opts.enabled)
                throwFilesystemDisabled("file_write", loc);
            auto resolved = resolveSandboxedPath("file_write", expectStr("file_write", args, 0, loc), opts, loc);
            const std::string &text = expectStr("file_write", args, 1, loc);
            std::ofstream out(resolved, std::ios::binary | std::ios::trunc);
            if (!out)
                return Value::makeBool(false);
            out << text;
            out.flush();
            return Value::makeBool(static_cast<bool>(out));
        };

        reg["file_add"] = [opts](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("file_add", args, 2, loc);
            if (!opts.enabled)
                throwFilesystemDisabled("file_add", loc);
            auto resolved = resolveSandboxedPath("file_add", expectStr("file_add", args, 0, loc), opts, loc);
            const std::string &text = expectStr("file_add", args, 1, loc);
            std::ofstream out(resolved, std::ios::binary | std::ios::app);
            if (!out)
                return Value::makeBool(false);
            out << text;
            out.flush();
            return Value::makeBool(static_cast<bool>(out));
        };

        reg["file_remove"] = [opts](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("file_remove", args, 1, loc);
            if (!opts.enabled)
                throwFilesystemDisabled("file_remove", loc);
            auto resolved = resolveSandboxedPath("file_remove", expectStr("file_remove", args, 0, loc), opts, loc);
            std::error_code ec;
            bool removed = std::filesystem::remove(resolved, ec);
            return Value::makeBool(removed && !ec);
        };
    }

} // namespace cuff
