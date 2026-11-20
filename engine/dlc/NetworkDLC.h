#pragma once

#include "DLCCommon.h"

// Building with -DCUFF_DISABLE_NETWORK leaves out the socket client (and its platform headers) for targets
// without sockets; `use DLC:network` still loads, and its functions fail with the same "unavailable" error.
#ifndef CUFF_DISABLE_NETWORK
#include "../net/HttpClient.h"
#endif

namespace cuff
{

    // A real (plain-HTTP-only) client, backed by engine/net/HttpClient.h — see
    // that file for the socket-level implementation and the SSRF guard.
    struct NetworkDLCOptions
    {
        bool enabled = true;
        bool allowPrivateTargets = false;
    };

#ifndef CUFF_DISABLE_NETWORK
    inline Value makeHttpResultMap(const net::HttpResponse &resp)
    {
        auto m = std::make_shared<ValueMap>();
        m->set("status", Value::makeNumber(resp.status));
        m->set("ok", Value::makeBool(resp.status >= 200 && resp.status < 300));
        m->set("body", Value::makeStr(resp.body));
        return Value::makeMap(std::move(m));
    }
#endif

    [[noreturn]] inline void throwNetworkDisabled(const char *fn, const SourceLocation &loc)
    {
        throw ModuleError(ErrorCode::DLCFeatureUnavailable,
                          std::string("DLC:network's ") + fn + "() is disabled for this run (network access was turned off by the host)",
                          loc, "the host embedding this engine controls this — see Engine::Options::networkEnabled");
    }

    [[noreturn]] inline void throwNetworkFailed(const char *fn, const std::string &detail, const SourceLocation &loc)
    {
        throw CuffRuntimeError(ErrorCode::NetworkRequestFailed,
                               std::string(fn) + "() failed: " + detail, loc);
    }

#ifdef CUFF_DISABLE_NETWORK
    inline void registerNetworkDLC(std::unordered_map<std::string, NativeFn> &reg, NetworkDLCOptions)
    {
        for (const char *name : {"network_get", "network_post"})
            reg[name] = [name](std::vector<Value> &, const SourceLocation &loc) -> Value
            {
                throw ModuleError(ErrorCode::DLCFeatureUnavailable,
                                  std::string("DLC:network's ") + name + "() is not available in this build (compiled with CUFF_DISABLE_NETWORK)",
                                  loc, "rebuild without CUFF_DISABLE_NETWORK to use network access");
            };
    }
#else
    // A request that is under way: the task that made it is set aside, and the other tasks run, until the answer
    // is complete.
    struct RequestWait final : Wait
    {
        const char *function;
        net::Request request;

        RequestWait(const char *fn, const std::string &method, const std::string &url, const std::string &body,
                    const std::string &contentType, const net::HttpOptions &opts)
            : function(fn), request(method, url, body, contentType, opts)
        {
        }

        bool advance() override { return request.advance(); }
        Value finish(const SourceLocation &loc) override
        {
            if (!request.result.ok)
                throwNetworkFailed(function, request.result.errorMessage, loc);
            return makeHttpResultMap(request.result);
        }
        Clock::time_point wakeAt() const override { return request.wakeAt(); }
        bool usesSocket() const override { return true; }
        long long socket() const override
        {
            return request.socket() == kCuffInvalidSocket ? -1 : static_cast<long long>(request.socket());
        }
        bool wantsWrite() const override { return request.wantsWrite(); }
        SocketPoller poller() const override { return &net::detail::pollSockets; }
    };

    // Makes the request: through the machine that is running, so that other tasks run while it is under way, or
    // right here when the function is called from outside a run.
    inline Value httpRequest(const char *function, const char *method, const std::string &url, const std::string &body,
                             const std::string &contentType, const NetworkDLCOptions &opts, const SourceLocation &loc)
    {
        net::HttpOptions httpOpts;
        httpOpts.allowPrivateTargets = opts.allowPrivateTargets;
        if (WaitHook *machine = activeWaitHook())
        {
            machine->waitFor(std::make_unique<RequestWait>(function, method, url, body, contentType, httpOpts));
            return Value();
        }
        net::HttpResponse resp = net::performRequest(method, url, body, contentType, httpOpts);
        if (!resp.ok)
            throwNetworkFailed(function, resp.errorMessage, loc);
        return makeHttpResultMap(resp);
    }

    inline void registerNetworkDLC(std::unordered_map<std::string, NativeFn> &reg, NetworkDLCOptions opts)
    {
        reg["network_get"] = [opts](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("network_get", args, 1, loc);
            if (!opts.enabled)
                throwNetworkDisabled("network_get", loc);
            return httpRequest("network_get", "GET", expectStr("network_get", args, 0, loc), std::string(), std::string(), opts, loc);
        };

        reg["network_post"] = [opts](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("network_post", args, 2, 3, loc);
            if (!opts.enabled)
                throwNetworkDisabled("network_post", loc);
            const std::string &url = expectStr("network_post", args, 0, loc);
            const std::string &body = expectStr("network_post", args, 1, loc);
            const std::string contentType = args.size() == 3 ? expectStr("network_post", args, 2, loc) : std::string();
            return httpRequest("network_post", "POST", url, body, contentType, opts, loc);
        };
    }
#endif

}
