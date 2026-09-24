#pragma once

// A minimal, dependency-free HTTP/1.1 client backing DLC:network's get()/
// post(). Plain HTTP only — there is no TLS here, so https:// URLs fail with
// a clear error rather than silently talking plaintext to an HTTPS port (see
// docs/SPEC.md and SECURITY.md for the rationale and how a host can bridge
// HTTPS itself). POSIX sockets are used on Linux and macOS; Winsock2 on
// Windows behind `#ifdef _WIN32`, so this is the one file in the engine with
// real platform-specific code.
//
// Safety by default: every resolved address is checked against loopback,
// private and link-local ranges (the classic SSRF targets — localhost admin
// panels, cloud metadata endpoints) and rejected unless the caller opts in
// via HttpOptions::allowPrivateTargets. This mirrors the module sandbox's
// "safe by default, widen explicitly" shape (see Interpreter's module root).

#include "../common/Limits.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using cuff_socket_t = SOCKET;
constexpr cuff_socket_t kCuffInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <unistd.h>
using cuff_socket_t = int;
constexpr cuff_socket_t kCuffInvalidSocket = -1;
#endif

namespace cuff::net
{

    struct HttpOptions
    {
        bool allowPrivateTargets = false;
        int connectTimeoutMs = limits::kHttpConnectTimeoutMs;
        int totalTimeoutMs = limits::kHttpTotalTimeoutMs;
        size_t maxResponseBytes = limits::kHttpMaxResponseBytes;
        int maxRedirects = limits::kHttpMaxRedirects;
        std::string userAgent = "CuffScript/1.7 (+DLC:network)";
    };

    struct HttpResponse
    {
        bool ok = false; // a full HTTP response was received (status may still be 4xx/5xx — that's still "ok")
        int status = 0;
        std::string body;
        std::string finalUrl;    // after following any redirects
        std::string errorMessage; // set when ok == false
    };

    namespace detail
    {

#if defined(_WIN32)
        // Winsock needs one-time process setup; a static instance's
        // constructor/destructor bracket the program's socket usage. Every
        // public entry point below touches this before creating a socket.
        struct WinsockInit
        {
            WinsockInit()
            {
                WSADATA data;
                WSAStartup(MAKEWORD(2, 2), &data);
            }
            ~WinsockInit() { WSACleanup(); }
        };
        inline void ensureInit() { static WinsockInit init; (void)init; }
        inline void closeSocket(cuff_socket_t s) { closesocket(s); }
        inline bool wouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; }
        inline bool inProgress() { return WSAGetLastError() == WSAEWOULDBLOCK; }
        inline void setNonBlocking(cuff_socket_t s)
        {
            u_long mode = 1;
            ioctlsocket(s, FIONBIO, &mode);
        }
        inline void setBlocking(cuff_socket_t s)
        {
            u_long mode = 0;
            ioctlsocket(s, FIONBIO, &mode);
        }
        inline void setTimeouts(cuff_socket_t s, int ms)
        {
            DWORD t = static_cast<DWORD>(ms);
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&t), sizeof t);
            setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&t), sizeof t);
        }
        inline int sendAll(cuff_socket_t s, const char *data, size_t len)
        {
            return send(s, data, static_cast<int>(len), 0);
        }
        inline int recvSome(cuff_socket_t s, char *buf, size_t len)
        {
            return recv(s, buf, static_cast<int>(len), 0);
        }
#else
        inline void ensureInit() {}
        inline void closeSocket(cuff_socket_t s) { ::close(s); }
        inline bool wouldBlock() { return errno == EWOULDBLOCK || errno == EAGAIN; }
        inline bool inProgress() { return errno == EINPROGRESS; }
        inline void setNonBlocking(cuff_socket_t s)
        {
            int flags = fcntl(s, F_GETFL, 0);
            fcntl(s, F_SETFL, flags | O_NONBLOCK);
        }
        inline void setBlocking(cuff_socket_t s)
        {
            int flags = fcntl(s, F_GETFL, 0);
            fcntl(s, F_SETFL, flags & ~O_NONBLOCK);
        }
        inline void setTimeouts(cuff_socket_t s, int ms)
        {
            struct timeval tv;
            tv.tv_sec = ms / 1000;
            tv.tv_usec = (ms % 1000) * 1000;
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
            setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#if defined(__APPLE__)
            int one = 1;
            setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one); // avoid SIGPIPE on write to a closed peer
#endif
        }
        inline int sendAll(cuff_socket_t s, const char *data, size_t len)
        {
#if defined(__linux__)
            return static_cast<int>(::send(s, data, len, MSG_NOSIGNAL));
#else
            return static_cast<int>(::send(s, data, len, 0)); // macOS: SIGPIPE already suppressed via SO_NOSIGPIPE above
#endif
        }
        inline int recvSome(cuff_socket_t s, char *buf, size_t len)
        {
            return static_cast<int>(::recv(s, buf, len, 0));
        }
#endif

        inline std::string toLower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
                            { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        struct ParsedUrl
        {
            std::string scheme;
            std::string host; // hostname or IP literal, brackets already stripped for IPv6
            int port = 80;
            std::string target; // path + query, defaults to "/"
        };

        // Deliberately minimal: no percent-decoding, no userinfo (user:pass@)
        // support, no fragment handling — a plain http://host[:port]/path
        // is what DLC:network is meant for.
        inline bool parseUrl(const std::string &url, ParsedUrl &out, std::string &err)
        {
            size_t schemeEnd = url.find("://");
            if (schemeEnd == std::string::npos)
            {
                err = "URL has no scheme (expected \"http://...\")";
                return false;
            }
            out.scheme = toLower(url.substr(0, schemeEnd));
            size_t pos = schemeEnd + 3;
            if (out.scheme != "http")
            {
                err = out.scheme == "https"
                          ? "https:// is not supported (this client is plain HTTP only — see docs/SPEC.md)"
                          : "unsupported URL scheme '" + out.scheme + "' (only http:// is supported)";
                return false;
            }

            size_t hostStart = pos;
            size_t pathStart;
            if (pos < url.size() && url[pos] == '[')
            {
                size_t closeBracket = url.find(']', pos);
                if (closeBracket == std::string::npos)
                {
                    err = "malformed IPv6 host in URL (missing ']')";
                    return false;
                }
                out.host = url.substr(hostStart + 1, closeBracket - hostStart - 1);
                pathStart = closeBracket + 1;
            }
            else
            {
                size_t end = url.find_first_of(":/", pos);
                if (end == std::string::npos)
                    end = url.size();
                out.host = url.substr(hostStart, end - hostStart);
                pathStart = end;
            }
            if (out.host.empty())
            {
                err = "URL has no host";
                return false;
            }

            if (pathStart < url.size() && url[pathStart] == ':')
            {
                size_t portStart = pathStart + 1;
                size_t portEnd = url.find('/', portStart);
                if (portEnd == std::string::npos)
                    portEnd = url.size();
                std::string portStr = url.substr(portStart, portEnd - portStart);
                if (portStr.empty() || !std::all_of(portStr.begin(), portStr.end(), [](unsigned char c)
                                                     { return std::isdigit(c); }))
                {
                    err = "invalid port in URL";
                    return false;
                }
                long p = std::strtol(portStr.c_str(), nullptr, 10);
                if (p <= 0 || p > 65535)
                {
                    err = "port out of range in URL";
                    return false;
                }
                out.port = static_cast<int>(p);
                pathStart = portEnd;
            }

            out.target = pathStart < url.size() ? url.substr(pathStart) : "/";
            if (out.target.empty())
                out.target = "/";
            return true;
        }

        // SSRF guard: true if `addr` names a loopback, private, link-local, or
        // otherwise non-public address (IPv4 and IPv6). Checked against the
        // concrete address just before connecting, not the hostname text, so
        // "example.com" that happens to resolve to 127.0.0.1 is still caught.
        inline bool isPrivateOrLoopback(const sockaddr *addr)
        {
            if (addr->sa_family == AF_INET)
            {
                uint32_t ip = ntohl(reinterpret_cast<const sockaddr_in *>(addr)->sin_addr.s_addr);
                uint8_t a = static_cast<uint8_t>(ip >> 24);
                uint8_t b = static_cast<uint8_t>(ip >> 16);
                if (a == 127 || a == 10 || a == 0)
                    return true;
                if (a == 169 && b == 254)
                    return true; // link-local, incl. cloud metadata (169.254.169.254)
                if (a == 172 && b >= 16 && b <= 31)
                    return true;
                if (a == 192 && b == 168)
                    return true;
                if (a == 100 && b >= 64 && b <= 127)
                    return true; // carrier-grade NAT
                return false;
            }
            if (addr->sa_family == AF_INET6)
            {
                const unsigned char *ip = reinterpret_cast<const sockaddr_in6 *>(addr)->sin6_addr.s6_addr;
                static const unsigned char loopback[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
                if (std::memcmp(ip, loopback, 16) == 0)
                    return true;
                if ((ip[0] & 0xFE) == 0xFC) // fc00::/7 unique local
                    return true;
                if (ip[0] == 0xFE && (ip[1] & 0xC0) == 0x80) // fe80::/10 link-local
                    return true;
                // ::ffff:0:0/96 — IPv4-mapped; re-check the embedded IPv4.
                static const unsigned char v4mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
                if (std::memcmp(ip, v4mapped, 12) == 0)
                {
                    sockaddr_in inner{};
                    inner.sin_family = AF_INET;
                    std::memcpy(&inner.sin_addr, ip + 12, 4);
                    return isPrivateOrLoopback(reinterpret_cast<const sockaddr *>(&inner));
                }
                return false;
            }
            return true; // unknown family: fail closed
        }

        inline bool connectWithTimeout(cuff_socket_t sock, const sockaddr *addr, socklen_t len, int timeoutMs)
        {
            setNonBlocking(sock);
            int rc = ::connect(sock, addr, len);
            if (rc == 0)
            {
                setBlocking(sock);
                return true;
            }
            if (!inProgress())
                return false;
            fd_set writeSet;
            FD_ZERO(&writeSet);
            FD_SET(sock, &writeSet);
            struct timeval tv;
            tv.tv_sec = timeoutMs / 1000;
            tv.tv_usec = (timeoutMs % 1000) * 1000;
            int sel = select(static_cast<int>(sock) + 1, nullptr, &writeSet, nullptr, &tv);
            if (sel <= 0)
                return false;
            int err = 0;
            socklen_t elen = sizeof err;
            if (getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&err), &elen) != 0 || err != 0)
                return false;
            setBlocking(sock);
            return true;
        }

        // Reads exactly `want` bytes (or until close if want == npos, capped
        // at `cap`), enforcing the overall deadline throughout.
        inline bool readBytes(cuff_socket_t sock, std::string &out, size_t want, size_t cap,
                              std::chrono::steady_clock::time_point deadline, std::string &err)
        {
            char buf[16384];
            while (want == std::string::npos || out.size() < want)
            {
                if (std::chrono::steady_clock::now() > deadline)
                {
                    err = "timed out reading the response";
                    return false;
                }
                int n = recvSome(sock, buf, sizeof buf);
                if (n < 0)
                {
                    if (wouldBlock())
                        continue; // recv timeout fired short of the deadline; loop and recheck
                    err = "connection error while reading the response";
                    return false;
                }
                if (n == 0)
                    break; // peer closed
                if (out.size() + static_cast<size_t>(n) > cap)
                {
                    err = "response exceeded the maximum allowed size";
                    return false;
                }
                out.append(buf, static_cast<size_t>(n));
            }
            return true;
        }

        inline bool readLine(cuff_socket_t sock, std::string &pending, std::string &line,
                             size_t cap, std::chrono::steady_clock::time_point deadline, std::string &err)
        {
            while (true)
            {
                size_t nl = pending.find('\n');
                if (nl != std::string::npos)
                {
                    line = pending.substr(0, nl);
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    pending.erase(0, nl + 1);
                    return true;
                }
                if (pending.size() > cap)
                {
                    err = "response header/line too large";
                    return false;
                }
                if (std::chrono::steady_clock::now() > deadline)
                {
                    err = "timed out reading the response";
                    return false;
                }
                char buf[4096];
                int n = recvSome(sock, buf, sizeof buf);
                if (n < 0)
                {
                    if (wouldBlock())
                        continue;
                    err = "connection error while reading the response";
                    return false;
                }
                if (n == 0)
                {
                    err = "connection closed unexpectedly";
                    return false;
                }
                pending.append(buf, static_cast<size_t>(n));
            }
        }

        struct RawResponse
        {
            int status = 0;
            std::vector<std::pair<std::string, std::string>> headers; // lowercased names
            std::string body;
        };

        inline const std::string *findHeader(const RawResponse &r, const char *name)
        {
            for (auto &h : r.headers)
                if (h.first == name)
                    return &h.second;
            return nullptr;
        }

        inline bool receiveResponse(cuff_socket_t sock, const HttpOptions &opts,
                                    std::chrono::steady_clock::time_point deadline,
                                    RawResponse &out, std::string &err)
        {
            std::string pending, line;
            if (!readLine(sock, pending, line, 8192, deadline, err))
                return false;
            // "HTTP/1.1 200 OK"
            size_t sp1 = line.find(' ');
            size_t sp2 = sp1 == std::string::npos ? std::string::npos : line.find(' ', sp1 + 1);
            if (sp1 == std::string::npos)
            {
                err = "malformed status line from server";
                return false;
            }
            out.status = std::atoi(line.substr(sp1 + 1, sp2 == std::string::npos ? std::string::npos : sp2 - sp1 - 1).c_str());

            while (true)
            {
                if (!readLine(sock, pending, line, 16384, deadline, err))
                    return false;
                if (line.empty())
                    break;
                size_t colon = line.find(':');
                if (colon == std::string::npos)
                    continue;
                std::string name = toLower(line.substr(0, colon));
                size_t vstart = colon + 1;
                while (vstart < line.size() && line[vstart] == ' ')
                    ++vstart;
                out.headers.emplace_back(std::move(name), line.substr(vstart));
            }

            const std::string *te = findHeader(out, "transfer-encoding");
            const std::string *cl = findHeader(out, "content-length");

            if (te && toLower(*te).find("chunked") != std::string::npos)
            {
                while (true)
                {
                    if (!readLine(sock, pending, line, 64, deadline, err))
                        return false;
                    size_t semi = line.find(';'); // chunk extensions, if any
                    std::string sizeStr = semi == std::string::npos ? line : line.substr(0, semi);
                    unsigned long chunkSize = std::strtoul(sizeStr.c_str(), nullptr, 16);
                    if (chunkSize == 0)
                    {
                        // Optional trailing headers, then the final blank line.
                        while (readLine(sock, pending, line, 16384, deadline, err) && !line.empty())
                        {
                        }
                        break;
                    }
                    if (out.body.size() + chunkSize > opts.maxResponseBytes)
                    {
                        err = "response exceeded the maximum allowed size";
                        return false;
                    }
                    while (pending.size() < chunkSize)
                    {
                        if (std::chrono::steady_clock::now() > deadline)
                        {
                            err = "timed out reading the response";
                            return false;
                        }
                        char buf[16384];
                        int n = recvSome(sock, buf, sizeof buf);
                        if (n < 0)
                        {
                            if (wouldBlock())
                                continue;
                            err = "connection error while reading the response";
                            return false;
                        }
                        if (n == 0)
                        {
                            err = "connection closed mid-chunk";
                            return false;
                        }
                        pending.append(buf, static_cast<size_t>(n));
                    }
                    out.body.append(pending, 0, chunkSize);
                    pending.erase(0, chunkSize);
                    // Each chunk is followed by a CRLF; consume it (may need one more line-read cycle).
                    if (!readLine(sock, pending, line, 8, deadline, err))
                        return false;
                }
            }
            else if (cl)
            {
                unsigned long long want = std::strtoull(cl->c_str(), nullptr, 10);
                if (want > opts.maxResponseBytes)
                {
                    err = "response exceeded the maximum allowed size";
                    return false;
                }
                out.body = std::move(pending);
                if (out.body.size() < want)
                {
                    if (!readBytes(sock, out.body, static_cast<size_t>(want), opts.maxResponseBytes, deadline, err))
                        return false;
                }
                else
                {
                    out.body.resize(static_cast<size_t>(want));
                }
            }
            else
            {
                out.body = std::move(pending);
                if (!readBytes(sock, out.body, std::string::npos, opts.maxResponseBytes, deadline, err))
                    return false;
            }
            return true;
        }

    } // namespace detail

    inline HttpResponse performRequest(const std::string &method, const std::string &initialUrl,
                                       const std::string &requestBody, const std::string &contentType,
                                       const HttpOptions &opts)
    {
        using namespace detail;
        ensureInit();

        HttpResponse result;
        std::string url = initialUrl;
        std::string method_ = method;
        std::string body = requestBody;
        const auto overallDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(opts.totalTimeoutMs);

        for (int redirectsLeft = opts.maxRedirects; redirectsLeft >= 0; --redirectsLeft)
        {
            ParsedUrl parsed;
            std::string perr;
            if (!parseUrl(url, parsed, perr))
            {
                result.errorMessage = perr;
                return result;
            }

            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            addrinfo *resolved = nullptr;
            std::string portStr = std::to_string(parsed.port);
            int gai = getaddrinfo(parsed.host.c_str(), portStr.c_str(), &hints, &resolved);
            if (gai != 0 || !resolved)
            {
                result.errorMessage = "could not resolve host '" + parsed.host + "'";
                return result;
            }

            cuff_socket_t sock = kCuffInvalidSocket;
            bool blockedByPolicy = false;
            for (addrinfo *p = resolved; p; p = p->ai_next)
            {
                if (!opts.allowPrivateTargets && isPrivateOrLoopback(p->ai_addr))
                {
                    blockedByPolicy = true;
                    continue;
                }
                cuff_socket_t s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
                if (s == kCuffInvalidSocket)
                    continue;
                if (connectWithTimeout(s, p->ai_addr, static_cast<socklen_t>(p->ai_addrlen), opts.connectTimeoutMs))
                {
                    sock = s;
                    break;
                }
                closeSocket(s);
            }
            freeaddrinfo(resolved);

            if (sock == kCuffInvalidSocket)
            {
                result.errorMessage = blockedByPolicy
                    ? "'" + parsed.host + "' resolves to a private/loopback address, which DLC:network blocks by default"
                    : "could not connect to '" + parsed.host + ":" + portStr + "'";
                return result;
            }

            setTimeouts(sock, opts.totalTimeoutMs);

            std::string hostHeader = parsed.host;
            if (parsed.port != 80)
                hostHeader += ":" + portStr;

            std::string request;
            request.reserve(256 + body.size());
            request += method_ + " " + parsed.target + " HTTP/1.1\r\n";
            request += "Host: " + hostHeader + "\r\n";
            request += "User-Agent: " + opts.userAgent + "\r\n";
            request += "Accept: */*\r\n";
            request += "Connection: close\r\n";
            if (!body.empty() || method_ == "POST")
            {
                request += "Content-Type: " + (contentType.empty() ? std::string("text/plain; charset=utf-8") : contentType) + "\r\n";
                request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
            }
            request += "\r\n";
            request += body;

            size_t sent = 0;
            bool sendFailed = false;
            while (sent < request.size())
            {
                if (std::chrono::steady_clock::now() > overallDeadline)
                {
                    result.errorMessage = "timed out sending the request";
                    sendFailed = true;
                    break;
                }
                int n = sendAll(sock, request.data() + sent, request.size() - sent);
                if (n <= 0)
                {
                    if (n < 0 && wouldBlock())
                        continue;
                    result.errorMessage = "connection error while sending the request";
                    sendFailed = true;
                    break;
                }
                sent += static_cast<size_t>(n);
            }
            if (sendFailed)
            {
                closeSocket(sock);
                return result;
            }

            RawResponse raw;
            std::string rerr;
            bool received = receiveResponse(sock, opts, overallDeadline, raw, rerr);
            closeSocket(sock);

            if (!received)
            {
                result.errorMessage = rerr;
                return result;
            }

            bool isRedirect = (raw.status == 301 || raw.status == 302 || raw.status == 303 ||
                               raw.status == 307 || raw.status == 308);
            const std::string *location = isRedirect ? findHeader(raw, "location") : nullptr;
            if (isRedirect && location && redirectsLeft > 0)
            {
                std::string next = *location;
                if (next.find("://") == std::string::npos)
                {
                    // Relative redirect: resolve against the current host.
                    next = "http://" + hostHeader + (next.empty() || next[0] != '/' ? "/" + next : next);
                }
                if (raw.status == 303 || ((raw.status == 301 || raw.status == 302) && method_ == "POST"))
                {
                    method_ = "GET";
                    body.clear();
                }
                url = next;
                continue; // follow it
            }

            result.ok = true;
            result.status = raw.status;
            result.body = std::move(raw.body);
            result.finalUrl = url;
            return result;
        }

        result.errorMessage = "too many redirects";
        return result;
    }

    inline HttpResponse get(const std::string &url, const HttpOptions &opts)
    {
        return performRequest("GET", url, std::string(), std::string(), opts);
    }

    inline HttpResponse post(const std::string &url, const std::string &body, const std::string &contentType,
                              const HttpOptions &opts)
    {
        return performRequest("POST", url, body, contentType, opts);
    }

} // namespace cuff::net
