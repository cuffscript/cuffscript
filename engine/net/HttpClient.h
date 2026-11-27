#pragma once

#include "../common/Attributes.h"
#include "../common/Limits.h"
#include "../common/Text.h"
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
        inline void prepareSocket(cuff_socket_t) {}
        inline int sendAll(cuff_socket_t s, const char *data, size_t len)
        {
            return send(s, data, static_cast<int>(len), 0);
        }
        inline int recvNow(cuff_socket_t s, char *buf, size_t len)
        {
            return recv(s, buf, static_cast<int>(len), 0);
        }
        // Waits until one of the sockets is ready or `ms` milliseconds have passed. A connection that failed shows
        // in the set of exceptions on Windows, not in the set of writable sockets.
        inline void pollSockets(SocketWait *items, size_t count, int ms)
        {
            fd_set readSet, writeSet, failSet;
            FD_ZERO(&readSet);
            FD_ZERO(&writeSet);
            FD_ZERO(&failSet);
            for (size_t k = 0; k < count; ++k)
            {
                const cuff_socket_t s = static_cast<cuff_socket_t>(items[k].socket);
                FD_SET(s, items[k].write ? &writeSet : &readSet);
                FD_SET(s, &failSet);
            }
            struct timeval tv;
            tv.tv_sec = ms / 1000;
            tv.tv_usec = (ms % 1000) * 1000;
            if (select(0, &readSet, &writeSet, &failSet, &tv) <= 0)
                return;
            for (size_t k = 0; k < count; ++k)
            {
                const cuff_socket_t s = static_cast<cuff_socket_t>(items[k].socket);
                items[k].ready = FD_ISSET(s, &readSet) || FD_ISSET(s, &writeSet) || FD_ISSET(s, &failSet);
            }
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
        inline void prepareSocket(cuff_socket_t s)
        {
#if defined(__APPLE__)
            int one = 1;
            setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one); // no SIGPIPE on a write to a peer that closed
#else
            (void)s;
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
        inline int recvNow(cuff_socket_t s, char *buf, size_t len)
        {
            return static_cast<int>(::recv(s, buf, len, 0));
        }
        // Waits until one of the sockets is ready or `ms` milliseconds have passed. poll, not select: select can
        // only watch descriptors below FD_SETSIZE (1024), and a program that hosts the engine may well have more
        // files open than that.
        inline void pollSockets(SocketWait *items, size_t count, int ms)
        {
            struct pollfd fds[limits::kMaxOpenRequests];
            if (count > limits::kMaxOpenRequests)
                count = limits::kMaxOpenRequests;
            for (size_t k = 0; k < count; ++k)
            {
                fds[k].fd = static_cast<int>(items[k].socket);
                fds[k].events = items[k].write ? POLLOUT : POLLIN;
                fds[k].revents = 0;
            }
            if (::poll(fds, static_cast<nfds_t>(count), ms) <= 0)
                return; // nothing is ready, or a signal arrived: the caller looks at the time and asks again
            for (size_t k = 0; k < count; ++k)
                items[k].ready = fds[k].revents != 0;
        }
#endif

        inline int millisecondsLeft(std::chrono::steady_clock::time_point deadline)
        {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
            return left <= 0 ? 0 : (left > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<int>(left));
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

        // Reads a response from the bytes as they arrive. feed() is called whenever there is more to look at and
        // says whether the response is complete, needs more bytes, or is not a response the client accepts.
        class ResponseReader
        {
        public:
            enum class Result
            {
                More,
                Complete,
                Failed
            };

            explicit ResponseReader(size_t maxBytes) : maxBytes_(maxBytes) {}

            RawResponse response;
            std::string error;

            // The bytes that arrived; `closed` when the server has closed the connection and no more will come.
            Result feed(const char *data, size_t size, bool closed)
            {
                buffer_.append(data, size);
                for (;;)
                {
                    const Result r = step(closed);
                    if (r != Result::More)
                        return r;
                    if (!progressed_)
                        return Result::More;
                }
            }

            // The time for the whole request ran out, or the connection broke (`what` says which).
            Result stopped(const char *what)
            {
                if (state_ == State::Trailers)
                    return Result::Complete; // the body is whole; what follows the last chunk carries nothing the client uses
                error = what;
                return Result::Failed;
            }

        private:
            enum class State
            {
                StatusLine,
                Headers,
                ChunkSize,
                ChunkData,
                ChunkEnd,
                Trailers,
                Counted, // a body of Content-Length bytes
                ToClose  // a body that ends when the connection does
            };

            State state_ = State::StatusLine;
            size_t maxBytes_;
            std::string buffer_;
            size_t pos_ = 0; // what is before this has been used
            size_t want_ = 0;
            bool progressed_ = false;

            Result fail(const char *what)
            {
                error = what;
                return Result::Failed;
            }

            // The next line, without its line end. A line longer than `cap` is not one the client reads.
            enum class Line
            {
                Ready,
                More,
                TooLong,
                Closed
            };
            Line line(std::string &out, size_t cap, bool closed)
            {
                const size_t nl = buffer_.find('\n', pos_);
                if (nl == std::string::npos)
                {
                    if (buffer_.size() - pos_ > cap)
                        return Line::TooLong;
                    return closed ? Line::Closed : Line::More;
                }
                if (nl - pos_ > cap + 1)
                    return Line::TooLong;
                out.assign(buffer_, pos_, nl - pos_);
                if (!out.empty() && out.back() == '\r')
                    out.pop_back();
                pos_ = nl + 1;
                return Line::Ready;
            }

            Result lineProblem(Line l)
            {
                if (l == Line::More)
                    return Result::More;
                return fail(l == Line::TooLong ? "response header/line too large" : "connection closed unexpectedly");
            }

            // Takes what the buffer holds into the body. A body larger than the client accepts is an error.
            bool takeBody(size_t count)
            {
                if (count > maxBytes_ - response.body.size())
                    return false;
                response.body.append(buffer_, pos_, count);
                pos_ += count;
                return true;
            }

            void forgetUsed()
            {
                buffer_.erase(0, pos_);
                pos_ = 0;
            }

            Result step(bool closed)
            {
                progressed_ = true;
                std::string text;
                switch (state_)
                {
                case State::StatusLine:
                {
                    const Line l = line(text, 8192, closed);
                    if (l != Line::Ready)
                        return progressed_ = false, lineProblem(l);
                    const size_t sp1 = text.find(' ');
                    const size_t sp2 = sp1 == std::string::npos ? std::string::npos : text.find(' ', sp1 + 1);
                    // The status is three digits (RFC 9112 section 4): anything else is not a response.
                    const size_t codeEnd = sp2 == std::string::npos ? text.size() : sp2;
                    if (sp1 == std::string::npos || codeEnd - sp1 - 1 != 3 || !ascii::isDigit(text[sp1 + 1]) || !ascii::isDigit(text[sp1 + 2]) ||
                        !ascii::isDigit(text[sp1 + 3]))
                        return fail("malformed status line from server");
                    response.status = (text[sp1 + 1] - '0') * 100 + (text[sp1 + 2] - '0') * 10 + (text[sp1 + 3] - '0');
                    state_ = State::Headers;
                    return Result::More;
                }
                case State::Headers:
                {
                    const Line l = line(text, 16384, closed);
                    if (l != Line::Ready)
                        return progressed_ = false, lineProblem(l);
                    if (!text.empty())
                    {
                        const size_t colon = text.find(':');
                        if (colon != std::string::npos)
                        {
                            size_t value = colon + 1;
                            while (value < text.size() && text[value] == ' ')
                                ++value;
                            response.headers.emplace_back(toLower(text.substr(0, colon)), text.substr(value));
                        }
                        return Result::More;
                    }
                    const std::string *encoding = findHeader(response, "transfer-encoding");
                    const std::string *length = findHeader(response, "content-length");
                    if (encoding && toLower(*encoding).find("chunked") != std::string::npos)
                    {
                        state_ = State::ChunkSize;
                    }
                    else if (length)
                    {
                        unsigned long long want = 0;
                        if (!parseSize(*length, 10, want))
                            return fail("malformed Content-Length in the response");
                        if (want > maxBytes_)
                            return fail("response exceeded the maximum allowed size");
                        want_ = static_cast<size_t>(want);
                        state_ = State::Counted;
                    }
                    else
                    {
                        state_ = State::ToClose;
                    }
                    return Result::More;
                }
                case State::ChunkSize:
                {
                    const Line l = line(text, 64, closed);
                    if (l != Line::Ready)
                        return progressed_ = false, lineProblem(l);
                    unsigned long long size = 0;
                    if (!parseSize(text.substr(0, text.find(';')), 16, size))
                        return fail("malformed chunk size in the response");
                    if (size == 0)
                    {
                        state_ = State::Trailers;
                        return Result::More;
                    }
                    // Compared against what is left of the budget: body size plus chunk size can wrap around.
                    if (size > maxBytes_ - response.body.size())
                        return fail("response exceeded the maximum allowed size");
                    want_ = static_cast<size_t>(size);
                    state_ = State::ChunkData;
                    return Result::More;
                }
                case State::ChunkData:
                {
                    if (buffer_.size() - pos_ < want_)
                    {
                        progressed_ = false;
                        return closed ? fail("connection closed mid-chunk") : Result::More;
                    }
                    takeBody(want_);
                    forgetUsed();
                    state_ = State::ChunkEnd;
                    return Result::More;
                }
                case State::ChunkEnd:
                {
                    const Line l = line(text, 8, closed);
                    if (l != Line::Ready)
                        return progressed_ = false, lineProblem(l);
                    state_ = State::ChunkSize;
                    return Result::More;
                }
                case State::Trailers:
                {
                    const Line l = line(text, 16384, closed);
                    if (l == Line::More)
                        return progressed_ = false, Result::More;
                    return l == Line::Ready && !text.empty() ? Result::More : Result::Complete;
                }
                case State::Counted:
                {
                    const size_t have = buffer_.size() - pos_;
                    const size_t missing = want_ - response.body.size();
                    takeBody(std::min(have, missing));
                    forgetUsed();
                    progressed_ = false;
                    // A server that closes early has sent what it had: the body is what arrived.
                    return response.body.size() == want_ || closed ? Result::Complete : Result::More;
                }
                case State::ToClose:
                {
                    if (!takeBody(buffer_.size() - pos_))
                        return fail("response exceeded the maximum allowed size");
                    forgetUsed();
                    progressed_ = false;
                    return closed ? Result::Complete : Result::More;
                }
                }
                return fail("connection error while reading the response");
            }
        };

    }

    // One request, carried out without ever waiting on the network: advance() does what can be done now and says
    // whether the request is over. Between calls the caller waits for socket() to become ready, or for wakeAt().
    // Finding the address of a host name is the one step that can hold the caller up.
    class Request
    {
    public:
        using Clock = std::chrono::steady_clock;

        Request(const std::string &method, const std::string &url, const std::string &body, const std::string &contentType,
                const HttpOptions &opts)
            : opts_(opts), method_(method), url_(url), body_(body), contentType_(contentType), redirectsLeft_(opts.maxRedirects),
              reader_(opts.maxResponseBytes)
        {
        }

        Request(const Request &) = delete;
        Request &operator=(const Request &) = delete;

        ~Request() { release(); }

        HttpResponse result;

        cuff_socket_t socket() const { return sock_; }
        bool wantsWrite() const { return phase_ == Phase::Connecting || phase_ == Phase::Sending; }
        Clock::time_point wakeAt() const { return phase_ == Phase::Connecting ? connectDeadline_ : deadline_; }

        // True when the request is over: `result` holds the response, or the reason there is none.
        bool advance()
        {
            using namespace detail;
            for (;;)
            {
                switch (phase_)
                {
                case Phase::Start:
                    ensureInit();
                    // One deadline for the whole request, so that following redirects cannot stretch it.
                    deadline_ = Clock::now() + std::chrono::milliseconds(opts_.totalTimeoutMs);
                    phase_ = Phase::Resolve;
                    break;
                case Phase::Resolve:
                    if (!resolve())
                        return finish();
                    phase_ = Phase::NextAddress;
                    break;
                case Phase::NextAddress:
                    if (!connectNext())
                        return finish();
                    break;
                case Phase::Connecting:
                {
                    int err = 0;
                    socklen_t len = sizeof err;
                    const bool known = getsockopt(sock_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&err), &len) == 0;
                    SocketWait probe{static_cast<long long>(sock_), true, false};
                    pollSockets(&probe, 1, 0);
                    if (!known || err != 0)
                    {
                        dropSocket();
                        phase_ = Phase::NextAddress;
                    }
                    else if (probe.ready)
                    {
                        connected();
                    }
                    else if (Clock::now() > connectDeadline_)
                    {
                        dropSocket();
                        phase_ = Phase::NextAddress;
                    }
                    else
                    {
                        return false;
                    }
                    break;
                }
                case Phase::Sending:
                {
                    if (Clock::now() > deadline_)
                        return fail("timed out sending the request");
                    const int n = sendAll(sock_, request_.data() + sent_, request_.size() - sent_);
                    if (n <= 0)
                    {
                        if (n < 0 && wouldBlock())
                            return false;
                        return fail("connection error while sending the request");
                    }
                    sent_ += static_cast<size_t>(n);
                    if (sent_ == request_.size())
                        phase_ = Phase::Receiving;
                    break;
                }
                case Phase::Receiving:
                {
                    char buf[16384];
                    ResponseReader::Result r;
                    if (Clock::now() > deadline_)
                    {
                        r = reader_.stopped("timed out reading the response");
                    }
                    else
                    {
                        const int n = recvNow(sock_, buf, sizeof buf);
                        if (n < 0 && wouldBlock())
                            return false;
                        if (n < 0)
                            r = reader_.stopped("connection error while reading the response");
                        else
                            r = reader_.feed(buf, static_cast<size_t>(n), n == 0);
                        if (n == 0 && r == ResponseReader::Result::More)
                            r = reader_.stopped("connection closed unexpectedly");
                    }
                    if (r == ResponseReader::Result::More)
                        break;
                    dropSocket();
                    if (r == ResponseReader::Result::Failed)
                        return fail(reader_.error);
                    if (!followRedirect())
                    {
                        result.ok = true;
                        result.status = reader_.response.status;
                        result.body = std::move(reader_.response.body);
                        result.finalUrl = url_;
                        return finish();
                    }
                    break;
                }
                case Phase::Done:
                    return true;
                }
            }
        }

    private:
        enum class Phase
        {
            Start,
            Resolve,
            NextAddress,
            Connecting,
            Sending,
            Receiving,
            Done
        };

        const HttpOptions opts_;
        std::string method_, url_, body_;
        const std::string contentType_;
        int redirectsLeft_;
        Phase phase_ = Phase::Start;
        Clock::time_point deadline_, connectDeadline_;

        detail::ParsedUrl parsed_;
        std::string port_;
        addrinfo *resolved_ = nullptr;
        addrinfo *next_ = nullptr;
        bool blockedByPolicy_ = false;
        cuff_socket_t sock_ = kCuffInvalidSocket;

        std::string request_;
        size_t sent_ = 0;
        detail::ResponseReader reader_;

        void dropSocket()
        {
            if (sock_ != kCuffInvalidSocket)
                detail::closeSocket(sock_);
            sock_ = kCuffInvalidSocket;
        }

        void release()
        {
            dropSocket();
            if (resolved_)
                freeaddrinfo(resolved_);
            resolved_ = next_ = nullptr;
        }

        bool finish()
        {
            release();
            phase_ = Phase::Done;
            return true;
        }

        bool fail(const std::string &why)
        {
            result.errorMessage = why;
            return finish();
        }

        bool resolve()
        {
            std::string err;
            parsed_ = detail::ParsedUrl();
            if (!detail::parseUrl(url_, parsed_, err))
            {
                result.errorMessage = err;
                return false;
            }
            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            port_ = std::to_string(parsed_.port);
            if (getaddrinfo(parsed_.host.c_str(), port_.c_str(), &hints, &resolved_) != 0 || !resolved_)
            {
                result.errorMessage = "could not resolve host '" + parsed_.host + "'";
                return false;
            }
            next_ = resolved_;
            blockedByPolicy_ = false;
            return true;
        }

        // Starts a connection to the next address the name stands for. False when there is none left.
        bool connectNext()
        {
            using namespace detail;
            for (; next_; next_ = next_->ai_next)
            {
                if (!opts_.allowPrivateTargets && isPrivateOrLoopback(next_->ai_addr))
                {
                    blockedByPolicy_ = true;
                    continue;
                }
                sock_ = ::socket(next_->ai_family, next_->ai_socktype, next_->ai_protocol);
                if (sock_ == kCuffInvalidSocket)
                    continue;
                // The connection is made without blocking, so that the time it may take is the client's to bound
                // and not the operating system's.
                setNonBlocking(sock_);
                prepareSocket(sock_);
                const int rc = ::connect(sock_, next_->ai_addr, static_cast<socklen_t>(next_->ai_addrlen));
                next_ = next_->ai_next;
                if (rc == 0)
                {
                    connected();
                    return true;
                }
                if (inProgress())
                {
                    connectDeadline_ = Clock::now() + std::chrono::milliseconds(opts_.connectTimeoutMs);
                    phase_ = Phase::Connecting;
                    return true;
                }
                dropSocket();
                return true; // the loop in advance() comes back for the address after this one
            }
            result.errorMessage = blockedByPolicy_
                                      ? "'" + parsed_.host + "' resolves to a private/loopback address, which DLC:network blocks by default"
                                      : "could not connect to '" + (parsed_.ipv6Literal ? "[" + parsed_.host + "]" : parsed_.host) + ":" + port_ + "'";
            return false;
        }

        void connected()
        {
            freeaddrinfo(resolved_);
            resolved_ = next_ = nullptr;

            std::string host = parsed_.ipv6Literal ? "[" + parsed_.host + "]" : parsed_.host;
            if (parsed_.port != 80)
                host += ":" + port_;
            request_.clear();
            request_.reserve(256 + body_.size());
            request_ += method_ + " " + parsed_.target + " HTTP/1.1\r\n";
            request_ += "Host: " + host + "\r\n";
            request_ += "User-Agent: " + opts_.userAgent + "\r\n";
            request_ += "Accept: */*\r\n";
            request_ += "Connection: close\r\n";
            if (!body_.empty() || method_ == "POST")
            {
                request_ += "Content-Type: " + (contentType_.empty() ? std::string("text/plain; charset=utf-8") : contentType_) + "\r\n";
                request_ += "Content-Length: " + std::to_string(body_.size()) + "\r\n";
            }
            request_ += "\r\n";
            request_ += body_;
            sent_ = 0;
            reader_ = detail::ResponseReader(opts_.maxResponseBytes);
            phase_ = Phase::Sending;
        }

        // True when the response sends the client on to another address, which the request then goes to.
        bool followRedirect()
        {
            const int status = reader_.response.status;
            const bool redirect = status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
            const std::string *location = redirect ? detail::findHeader(reader_.response, "location") : nullptr;
            if (!location)
                return false;
            if (redirectsLeft_ <= 0)
            {
                // The last answer the client may follow is given as it is.
                return false;
            }
            --redirectsLeft_;
            const std::string next = detail::resolveReference(url_, *location);
            // 303 always becomes a GET, and 301 and 302 turn a POST into one; 307 and 308 keep the method and the body.
            if (status == 303 || ((status == 301 || status == 302) && method_ == "POST"))
            {
                method_ = "GET";
                body_.clear();
            }
            url_ = next;
            phase_ = Phase::Resolve;
            return true;
        }
    };

    // Carries a request out and waits for it.
    inline HttpResponse performRequest(const std::string &method, const std::string &url, const std::string &body,
                                       const std::string &contentType, const HttpOptions &opts)
    {
        Request request(method, url, body, contentType, opts);
        while (!request.advance())
        {
            SocketWait wait{static_cast<long long>(request.socket()), request.wantsWrite(), false};
            detail::pollSockets(&wait, 1, detail::millisecondsLeft(request.wakeAt()) + 1);
        }
        return std::move(request.result);
    }

}
