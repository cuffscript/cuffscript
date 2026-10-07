#pragma once

#include "DLCCommon.h"

#include "../net/HttpClient.h"

namespace cuff
{

    // A real (plain-HTTP-only) client, backed by engine/net/HttpClient.h — see
    // that file for the socket-level implementation and the SSRF guard.
    struct NetworkDLCOptions
    {
        bool enabled = true;
        bool allowPrivateTargets = false;
    };

    inline Value makeHttpResultMap(const net::HttpResponse &resp)
    {
        auto m = std::make_shared<ValueMap>();
        m->set("status", Value::makeNumber(resp.status));
        m->set("ok", Value::makeBool(resp.status >= 200 && resp.status < 300));
        m->set("body", Value::makeStr(resp.body));
        return Value::makeMap(std::move(m));
    }

    [[noreturn]] inline void throwNetworkDisabled(const char *fn, const SourceLocation &loc)
    {
        throw ModuleError(ErrorCode::DLCFeatureUnavailable,
                          std::string("DLC:network's ") + fn + "() is disabled for this run (network access was turned off by the host)",
                          loc, "the host embedding this engine controls this — see CuffEngine::Options::networkEnabled");
    }

    [[noreturn]] inline void throwNetworkFailed(const char *fn, const std::string &detail, const SourceLocation &loc)
    {
        throw CuffRuntimeError(ErrorCode::NetworkRequestFailed,
                               std::string(fn) + "() failed: " + detail, loc);
    }

    inline void registerNetworkDLC(std::unordered_map<std::string, NativeFn> &reg, NetworkDLCOptions opts)
    {
        reg["network_get"] = [opts](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("network_get", args, 1, loc);
            if (!opts.enabled)
                throwNetworkDisabled("network_get", loc);
            const std::string &url = expectStr("network_get", args, 0, loc);
            net::HttpOptions httpOpts;
            httpOpts.allowPrivateTargets = opts.allowPrivateTargets;
            net::HttpResponse resp = net::get(url, httpOpts);
            if (!resp.ok)
                throwNetworkFailed("network_get", resp.errorMessage, loc);
            return makeHttpResultMap(resp);
        };

        reg["network_post"] = [opts](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("network_post", args, 2, 3, loc);
            if (!opts.enabled)
                throwNetworkDisabled("network_post", loc);
            const std::string &url = expectStr("network_post", args, 0, loc);
            const std::string &body = expectStr("network_post", args, 1, loc);
            std::string contentType = args.size() == 3 ? expectStr("network_post", args, 2, loc) : std::string();
            net::HttpOptions httpOpts;
            httpOpts.allowPrivateTargets = opts.allowPrivateTargets;
            net::HttpResponse resp = net::post(url, body, contentType, httpOpts);
            if (!resp.ok)
                throwNetworkFailed("network_post", resp.errorMessage, loc);
            return makeHttpResultMap(resp);
        };
    }

}
