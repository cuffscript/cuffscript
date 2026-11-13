#pragma once

#include "../common/Ascii.h"
#include "../common/Limits.h"
#include <algorithm>
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
#include <poll.h>
#include <sys/time.h>
#include <sys/socket.h>
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
        std::string userAgent = "CuffScript (+DLC:network)";
    };

    struct HttpResponse
    {
        bool ok = false;
        int status = 0;
        std::string body;
        std::string finalUrl;
        std::string errorMessage;
    };

    namespace detail
    {

#if defined(_WIN32)
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
        // Waits for `s` to become readable or writable, until `ms` milliseconds have passed. A single socket in an
        // fd_set is safe on Windows, where the set holds handles and not bit positions.
        inline bool waitFor(cuff_socket_t s, bool write, int ms)
        {
            fd_set set;
            FD_ZERO(&set);
            FD_SET(s, &set);
            struct timeval tv;
            tv.tv_sec = ms / 1000;
            tv.tv_usec = (ms % 1000) * 1000;
            return select(0, write ? nullptr : &set, write ? &set : nullptr, nullptr, &tv) > 0;
        }
        inline void markTimedOut() { WSASetLastError(WSAEWOULDBLOCK); }
        inline int recvNow(cuff_socket_t s, char *buf, size_t len)
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
            setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);  // avoid SIGPIPE on write to a closed peer
#endif
        }
        inline int sendAll(cuff_socket_t s, const char *data, size_t len)
        {
#if defined(__linux__)
            return static_cast<int>(::send(s, data, len, MSG_NOSIGNAL));
#else
            return static_cast<int>(::send(s, data, len, 0));  // macOS: SIGPIPE already suppressed via SO_NOSIGPIPE above
#endif
        }
        // poll, not select: select can only watch descriptors below FD_SETSIZE (1024), and a program that hosts the
        // engine may well have more files open than that.
        inline bool waitFor(cuff_socket_t s, bool write, int ms)
        {
            struct pollfd p;
            p.fd = s;
            p.events = write ? POLLOUT : POLLIN;
            p.revents = 0;
            int rc;
            do
                rc = ::poll(&p, 1, ms);
            while (rc < 0 && errno == EINTR);
            return rc > 0;
        }
        inline void markTimedOut() { errno = EAGAIN; }
        inline int recvNow(cuff_socket_t s, char *buf, size_t len)
        {
            return static_cast<int>(::recv(s, buf, len, 0));
        }
#endif

        inline int millisecondsLeft(std::chrono::steady_clock::time_point deadline)
        {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
            return left <= 0 ? 0 : (left > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<int>(left));
        }

        // Reads what has arrived, waiting no later than `deadline`. When nothing arrives in time it fails the way a
        // non-blocking read with nothing to read fails (wouldBlock()), and the caller's own check of the deadline ends it.
        inline int recvSome(cuff_socket_t s, char *buf, size_t len, std::chrono::steady_clock::time_point deadline)
        {
            if (!waitFor(s, false, millisecondsLeft(deadline)))
            {
                markTimedOut();
                return -1;
            }
            return recvNow(s, buf, len);
        }

        inline std::string toLower(std::string s)
        {
            for (char &c : s)
                c = ascii::lower(c);
            return s;
        }

        struct ParsedUrl
        {
            std::string scheme;
            std::string host;
            int port = 80;
            std::string target;
            bool ipv6Literal = false;
        };

        // Deliberately minimal: plain http://host[:port][/path][?query] only (no percent-decoding or userinfo). A
        // fragment (#...) belongs to the client and is not sent.
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
                          ? "https:// is not supported (this client is plain HTTP only — see docs/reference/dlc/network.md)"
                          : "unsupported URL scheme '" + out.scheme + "' (only http:// is supported)";
                return false;
            }

            size_t hostStart = pos;
            size_t pathStart;
            // IPv6 literals use [addr]:port so ':' inside the address is not
            // mistaken for the port separator.
            if (pos < url.size() && url[pos] == '[')
            {
                size_t closeBracket = url.find(']', pos);
                if (closeBracket == std::string::npos)
                {
                    err = "malformed IPv6 host in URL (missing ']')";
                    return false;
                }
                out.host = url.substr(hostStart + 1, closeBracket - hostStart - 1);
                out.ipv6Literal = true;
                pathStart = closeBracket + 1;
            }
            else
            {
                size_t end = url.find_first_of(":/?#", pos);
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
                size_t portEnd = url.find_first_of("/?#", portStart);
                if (portEnd == std::string::npos)
                    portEnd = url.size();
                std::string portStr = url.substr(portStart, portEnd - portStart);
                if (portStr.empty() || !std::all_of(portStr.begin(), portStr.end(), [](unsigned char c)
                                                     { return ascii::isDigit(c); }))
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
            const size_t fragment = out.target.find('#');
            if (fragment != std::string::npos)
                out.target.erase(fragment);
            if (out.target.empty() || out.target[0] != '/')
                out.target.insert(0, "/");
            return true;
        }

        // RFC 3986, section 5.2.4: a path with its "." and ".." segments worked out.
        inline std::string removeDotSegments(std::string in)
        {
            std::string out;
            while (!in.empty())
            {
                if (in.compare(0, 3, "../") == 0)
                    in.erase(0, 3);
                else if (in.compare(0, 2, "./") == 0)
                    in.erase(0, 2);
                else if (in.compare(0, 3, "/./") == 0)
                    in.erase(0, 2);
                else if (in == "/.")
                    in = "/";
                else if (in.compare(0, 4, "/../") == 0 || in == "/..")
                {
                    in = in.size() == 3 ? std::string("/") : in.substr(3);
                    const size_t cut = out.rfind('/');
                    out.erase(cut == std::string::npos ? 0 : cut);
                }
                else if (in == "." || in == "..")
                    in.clear();
                else
                {
                    const size_t next = in.find('/', in[0] == '/' ? 1 : 0);
                    const size_t take = next == std::string::npos ? in.size() : next;
                    out.append(in, 0, take);
                    in.erase(0, take);
                }
            }
            return out;
        }

        // Where a `Location` points, resolved against the address that was requested as RFC 3986 (section 5.2)
        // resolves a reference: "b" next to "/a/c" is "/a/b", "../b" goes up, "?q" keeps the path, "//host/x"
        // keeps the scheme, and a reference with a scheme of its own stands alone.
        inline std::string resolveReference(const std::string &base, const std::string &reference)
        {
            std::string ref = reference.substr(0, reference.find('#'));
            size_t colon = 0;
            if (!ref.empty() && ascii::isLetter(static_cast<unsigned char>(ref[0])))
            {
                colon = 1;
                while (colon < ref.size() && (ascii::isLetterOrDigit(static_cast<unsigned char>(ref[colon])) ||
                                              ref[colon] == '+' || ref[colon] == '-' || ref[colon] == '.'))
                    ++colon;
                if (colon < ref.size() && ref[colon] == ':')
                    return ref;
            }
            const size_t schemeEnd = base.find("://");
            if (schemeEnd == std::string::npos)
                return ref;
            if (ref.compare(0, 2, "//") == 0)
                return base.substr(0, schemeEnd + 1) + ref;
            const size_t pathStart = base.find_first_of("/?#", schemeEnd + 3);
            const std::string origin = base.substr(0, pathStart);
            std::string rest = pathStart == std::string::npos ? std::string() : base.substr(pathStart);
            rest = rest.substr(0, rest.find('#'));
            const size_t baseQuery = rest.find('?');
            const std::string basePath = rest.substr(0, baseQuery);
            if (ref.empty())
                return origin + rest;
            if (ref[0] == '?')
                return origin + (basePath.empty() ? std::string("/") : basePath) + ref;
            std::string merged;
            if (ref[0] == '/')
                merged = ref;
            else if (basePath.empty())
                merged = "/" + ref;
            else
                merged = basePath.substr(0, basePath.rfind('/') + 1) + ref;
            const size_t query = merged.find('?');
            return origin + removeDotSegments(merged.substr(0, query)) +
                   (query == std::string::npos ? std::string() : merged.substr(query));
        }

        // SSRF guard: true for loopback, private, link-local and other
        // non-public addresses. The check is performed on the resolved address,
        // not the hostname, so a public-looking hostname cannot bypass the policy
        // by resolving to a private address.

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
                    return true;
                if (a == 172 && b >= 16 && b <= 31)
                    return true;
                if (a == 192 && b == 168)
                    return true;
                if (a == 100 && b >= 64 && b <= 127)
                    return true;
                return false;
            }
            if (addr->sa_family == AF_INET6)
            {
                const unsigned char *ip = reinterpret_cast<const sockaddr_in6 *>(addr)->sin6_addr.s6_addr;
                // `::` (unspecified) connects to the local host on Linux, so it counts as loopback too.
                static const unsigned char unspecified[16] = {};
                static const unsigned char loopback[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
                if (std::memcmp(ip, unspecified, 16) == 0 || std::memcmp(ip, loopback, 16) == 0)
                    return true;
                if ((ip[0] & 0xFE) == 0xFC)
                    return true;
                if (ip[0] == 0xFE && (ip[1] & 0xC0) == 0x80)
                    return true;
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
            return true;
        }

        // Use a non-blocking connect so the TCP handshake is bounded by our
        // explicit timeout rather than the operating system's default timeout.
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
            if (!waitFor(sock, true, timeoutMs))
                return false;
            int err = 0;
            socklen_t elen = sizeof err;
            if (getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&err), &elen) != 0 || err != 0)
                return false;
            setBlocking(sock);
            return true;
        }

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
                int n = recvSome(sock, buf, sizeof buf, deadline);
                if (n < 0)
                {
                    if (wouldBlock())
                        continue;
                    err = "connection error while reading the response";
                    return false;
                }
                if (n == 0)
                    break;
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
                int n = recvSome(sock, buf, sizeof buf, deadline);
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
            std::vector<std::pair<std::string, std::string>> headers;
            std::string body;
        };

        // A size the server wrote: digits in `base` (10 or 16) with blanks around them, and nothing else. Anything more
        // than 15 digits is larger than any response the client accepts.
        inline bool parseSize(const std::string &text, int base, unsigned long long &out)
        {
            size_t b = text.find_first_not_of(" \t");
            size_t e = text.find_last_not_of(" \t");
            if (b == std::string::npos || e - b + 1 > 15)
                return false;
            unsigned long long v = 0;
            for (size_t i = b; i <= e; ++i)
            {
                const char c = text[i];
                unsigned digit;
                if (c >= '0' && c <= '9')
                    digit = static_cast<unsigned>(c - '0');
                else if (base == 16 && c >= 'a' && c <= 'f')
                    digit = static_cast<unsigned>(c - 'a' + 10);
                else if (base == 16 && c >= 'A' && c <= 'F')
                    digit = static_cast<unsigned>(c - 'A' + 10);
                else
                    return false;
                v = v * static_cast<unsigned>(base) + digit;
            }
            out = v;
            return true;
        }

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
                    unsigned long long chunkSize = 0;
                    if (!parseSize(line.substr(0, line.find(';')), 16, chunkSize))
                    {
                        err = "malformed chunk size in the response";
                        return false;
                    }
                    if (chunkSize == 0)
                    {
                        while (readLine(sock, pending, line, 16384, deadline, err) && !line.empty())
                        {
                        }
                        break;
                    }
                    // Compare against the remaining budget: body.size() + chunkSize can wrap around for a hostile size.
                    if (chunkSize > opts.maxResponseBytes - out.body.size())
                    {
                        err = "response exceeded the maximum allowed size";
                        return false;
                    }
                    while (pending.size() < static_cast<size_t>(chunkSize))
                    {
                        if (std::chrono::steady_clock::now() > deadline)
                        {
                            err = "timed out reading the response";
                            return false;
                        }
                        char buf[16384];
                        int n = recvSome(sock, buf, sizeof buf, deadline);
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
                    out.body.append(pending, 0, static_cast<size_t>(chunkSize));
                    pending.erase(0, static_cast<size_t>(chunkSize));
                    if (!readLine(sock, pending, line, 8, deadline, err))
                        return false;
                }
            }
            else if (cl)
            {
                unsigned long long want = 0;
                if (!parseSize(*cl, 10, want))
                {
                    err = "malformed Content-Length in the response";
                    return false;
                }
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

    }

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
        // Keep one deadline for the request so response processing cannot extend
        // the caller's total timeout indefinitely.
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
                    : "could not connect to '" + (parsed.ipv6Literal ? "[" + parsed.host + "]" : parsed.host) + ":" + portStr + "'";
                return result;
            }

            setTimeouts(sock, opts.totalTimeoutMs);

            std::string hostHeader = parsed.ipv6Literal ? "[" + parsed.host + "]" : parsed.host;
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
                const std::string next = resolveReference(url, *location);
                // Follow common redirect semantics: 303 switches to GET, 301/302
                // switch POST to GET, while 307/308 preserve the original method/body.

                if (raw.status == 303 || ((raw.status == 301 || raw.status == 302) && method_ == "POST"))
                {
                    method_ = "GET";
                    body.clear();
                }
                url = next;
                continue;
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

}
