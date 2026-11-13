#include "engine/CuffEngine.h"
#include "engine/net/HttpClient.h"
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

using cuff::CuffEngine;
namespace fs = std::filesystem;

namespace
{
    int pass = 0, fail = 0;

    void check(bool cond, const std::string &msg)
    {
        if (cond)
            ++pass;
        else
        {
            ++fail;
            std::cout << "FAIL: " << msg << "\n";
        }
    }

#if !defined(_WIN32)
    // A request made while the process has more than FD_SETSIZE (1024) descriptors open: the socket's number is then
    // above 1024, which select() cannot watch and poll() can. A server on a thread answers once.
    void requestWithManyFilesOpen()
    {
        struct rlimit lim;
        if (getrlimit(RLIMIT_NOFILE, &lim) != 0)
            return;
        if (lim.rlim_cur < 1200)
        {
            struct rlimit raised = lim;
            raised.rlim_cur = lim.rlim_max == RLIM_INFINITY || lim.rlim_max >= 1200 ? 1200 : lim.rlim_max;
            setrlimit(RLIMIT_NOFILE, &raised);
        }
        std::vector<int> held;
        for (int i = 0; i < 1100; ++i)
        {
            const int fd = open("/dev/null", O_RDONLY);
            if (fd < 0)
                break;
            held.push_back(fd);
        }
        if (held.empty() || held.back() < 1100)
        {
            std::cout << "SKIP: could not open more than 1100 files, so the request above FD_SETSIZE was not made\n";
            for (int fd : held)
                close(fd);
            return;
        }
        const int listener = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t len = sizeof addr;
        if (listener < 0 || bind(listener, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0 || listen(listener, 1) != 0 ||
            getsockname(listener, reinterpret_cast<sockaddr *>(&addr), &len) != 0)
        {
            std::cout << "SKIP: could not listen on the loopback address\n";
            for (int fd : held)
                close(fd);
            return;
        }
        std::thread server([listener]()
                           {
                               const int c = accept(listener, nullptr, nullptr);
                               if (c < 0)
                                   return;
                               std::string request;
                               char buf[1024];
                               while (request.find("\r\n\r\n") == std::string::npos)
                               {
                                   const ssize_t n = recv(c, buf, sizeof buf, 0);
                                   if (n <= 0)
                                       break;
                                   request.append(buf, static_cast<size_t>(n));
                               }
                               const char reply[] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
                               send(c, reply, sizeof reply - 1, 0);
                               close(c);
                           });
        CuffEngine::Options options;
        options.allowPrivateNetworkTargets = true;
        const std::string url = "http://127.0.0.1:" + std::to_string(ntohs(addr.sin_port)) + "/";
        auto r = CuffEngine::execute("use DLC:network\nprint(network_get(\"" + url + "\")[\"body\"])\n", ".", options);
        server.join();
        close(listener);
        for (int fd : held)
            close(fd);
        check(r.success, "a request works with more than 1024 files open: " + r.error.substr(0, 160));
    }
#endif

    bool blockedV4(uint32_t hostOrderIp)
    {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(hostOrderIp);
        return cuff::net::detail::isPrivateOrLoopback(reinterpret_cast<const sockaddr *>(&a));
    }

    bool blockedV6(const unsigned char (&bytes)[16])
    {
        sockaddr_in6 a{};
        a.sin6_family = AF_INET6;
        std::memcpy(&a.sin6_addr, bytes, 16);
        return cuff::net::detail::isPrivateOrLoopback(reinterpret_cast<const sockaddr *>(&a));
    }
}

int main()
{
    // ---- DLC:network address policy ----
    check(blockedV4(0x7F000001), "127.0.0.1 is blocked");
    check(blockedV4(0x0A010203), "10.1.2.3 is blocked");
    check(blockedV4(0x00000000), "0.0.0.0 is blocked");
    check(blockedV4(0xAC100001), "172.16.0.1 is blocked");
    check(!blockedV4(0xAC0F0001), "172.15.0.1 is public");
    check(!blockedV4(0x08080808), "8.8.8.8 is public");

    const unsigned char unspecified[16] = {};
    const unsigned char loopback[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    const unsigned char linkLocal[16] = {0xFE, 0x80};
    const unsigned char uniqueLocal[16] = {0xFC, 0x00};
    const unsigned char publicV6[16] = {0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0, 0, 0, 0, 0, 0, 0, 0, 0x88, 0x88};
    const unsigned char mappedLoopback[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 127, 0, 0, 1};
    const unsigned char mappedPublic[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 8, 8, 8, 8};
    check(blockedV6(unspecified), "[::] (reaches localhost on Linux) is blocked");
    check(blockedV6(loopback), "[::1] is blocked");
    check(blockedV6(linkLocal), "fe80:: is blocked");
    check(blockedV6(uniqueLocal), "fc00:: is blocked");
    check(blockedV6(mappedLoopback), "::ffff:127.0.0.1 is blocked");
    check(!blockedV6(publicV6), "a public IPv6 address is allowed");
    check(!blockedV6(mappedPublic), "::ffff:8.8.8.8 is allowed");

    // ---- DLC:network: what a URL asks for, and where a redirect goes ----
    {
        using cuff::net::detail::ParsedUrl;
        auto parsed = [](const std::string &url)
        {
            ParsedUrl u;
            std::string err;
            if (!cuff::net::detail::parseUrl(url, u, err))
                return std::string("error: ") + err;
            return u.host + "|" + std::to_string(u.port) + "|" + u.target + (u.ipv6Literal ? "|v6" : "");
        };
        check(parsed("http://example.com") == "example.com|80|/", "a URL with no path asks for /");
        check(parsed("http://example.com/a/b?x=1") == "example.com|80|/a/b?x=1", "the path and the query are the target");
        check(parsed("http://example.com?x=1") == "example.com|80|/?x=1", "a query right after the host is not part of the host");
        check(parsed("http://example.com:8080?x=1") == "example.com|8080|/?x=1", "a query right after the port is not part of the port");
        check(parsed("http://example.com/p#top") == "example.com|80|/p", "a fragment is not sent");
        check(parsed("http://example.com#top") == "example.com|80|/", "a fragment right after the host is not part of the host");
        check(parsed("http://[::1]:8080/x") == "::1|8080|/x|v6", "an IPv6 address is written in brackets");

        auto resolve = cuff::net::detail::resolveReference;
        const std::string base = "http://example.com/a/b/c?q=1#f";
        check(resolve(base, "d") == "http://example.com/a/b/d", "a relative path replaces the last segment");
        check(resolve(base, "./d/") == "http://example.com/a/b/d/", "./ is the same folder");
        check(resolve(base, "../d") == "http://example.com/a/d", "../ goes up one folder");
        check(resolve(base, "../../../../d") == "http://example.com/d", "../ stops at the root");
        check(resolve(base, "/d?x=2") == "http://example.com/d?x=2", "an absolute path keeps the host");
        check(resolve(base, "?x=2") == "http://example.com/a/b/c?x=2", "a query alone keeps the path");
        check(resolve(base, "#g") == "http://example.com/a/b/c?q=1", "a fragment alone is the same address");
        check(resolve(base, "//other.org/x") == "http://other.org/x", "a reference that starts with // keeps the scheme");
        check(resolve(base, "http://other.org/x") == "http://other.org/x", "a reference with a scheme stands alone");
        check(resolve(base, "https://other.org/x") == "https://other.org/x", "including https, which the request then refuses");
        check(resolve(base, "/go?to=http://x.org") == "http://example.com/go?to=http://x.org", "a :// in the query does not make it absolute");
        check(resolve("http://example.com", "d") == "http://example.com/d", "a base without a path resolves against /");
        check(resolve("http://[::1]:8080/a/b", "c") == "http://[::1]:8080/a/c", "the host keeps its brackets and port");
    }

#if !defined(_WIN32)
    requestWithManyFilesOpen();
#endif

    // ---- DLC:filesystem: file_remove deletes files only ----
    fs::path root = fs::temp_directory_path() / "cuff_dlc_guards_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "emptydir");
    std::ofstream(root / "victim.txt") << "x";

    auto r = CuffEngine::execute("use DLC:filesystem\nfile_remove(\"emptydir\")\nfile_remove(\"victim.txt\")\n", root.string());
    check(r.success, "file_remove script runs: " + r.error.substr(0, 120));
    check(fs::is_directory(root / "emptydir"), "file_remove leaves an empty directory alone");
    check(!fs::exists(root / "victim.txt"), "file_remove still deletes a regular file");

    fs::remove_all(root, ec);

    // ---- DLC:filesystem: the folder functions keep to the sandbox and to what they promise ----
    fs::path outside = fs::temp_directory_path() / "cuff_dlc_guards_outside";
    fs::remove_all(root, ec);
    fs::remove_all(outside, ec);
    fs::create_directories(root / "full");
    fs::create_directories(outside);
    std::ofstream(root / "full" / "kept.txt") << "x";
    std::ofstream(outside / "secret.txt") << "s";

    auto run = [&](const std::string &body, bool filesystemOn = true)
    {
        CuffEngine::Options options;
        options.filesystemEnabled = filesystemOn;
        return CuffEngine::execute("use DLC:filesystem\n" + body, root.string(), options);
    };

    auto r2 = run("file_removedir(\"full\")\nfile_removedir(\".\")\nfile_move(\".\", \"moved_root\")\n");
    check(r2.success, "root and non-empty folder script runs: " + r2.error.substr(0, 120));
    check(fs::exists(root / "full" / "kept.txt"), "file_removedir leaves a folder that has something in it");
    check(fs::is_directory(root) && !fs::exists(root.parent_path() / "moved_root"), "the allowed folder is never moved or removed");

    for (const char *call : {"file_isdir", "file_list", "file_makedir", "file_removedir"})
    {
        auto off = run(std::string(call) + "(\"full\")\n", false);
        check(!off.success && off.error.find("E5-005") != std::string::npos, std::string(call) + " is refused when the filesystem is off");
    }
    for (const char *call : {"file_copy", "file_move"})
    {
        auto off = run(std::string(call) + "(\"full/kept.txt\", \"full/other.txt\")\n", false);
        check(!off.success && off.error.find("E5-005") != std::string::npos, std::string(call) + " is refused when the filesystem is off");
    }
    for (const char *call : {"file_isdir(\"../x\")", "file_list(\"../x\")", "file_makedir(\"../x\")", "file_removedir(\"../x\")",
                             "file_copy(\"../x\", \"y\")", "file_copy(\"full/kept.txt\", \"../y\")", "file_move(\"../x\", \"y\")",
                             "file_move(\"full/kept.txt\", \"../y\")"})
    {
        auto esc = run(std::string(call) + "\n");
        check(!esc.success && esc.error.find("E5-008") != std::string::npos, std::string(call) + " cannot leave the allowed folder");
    }

    // A link inside the folder that points outside is as far out as a path with "../".
    std::error_code linkError;
    fs::create_directory_symlink(outside, root / "link", linkError);
    if (!linkError)
    {
        for (const char *call : {"file_list(\"link\")", "file_makedir(\"link/new\")", "file_copy(\"link/secret.txt\", \"stolen.txt\")",
                                 "file_move(\"link/secret.txt\", \"stolen.txt\")", "file_removedir(\"link\")"})
        {
            auto esc = run(std::string(call) + "\n");
            check(!esc.success && esc.error.find("E5-008") != std::string::npos, std::string(call) + " is refused through a link that leaves the folder");
        }
        check(fs::exists(outside / "secret.txt") && !fs::exists(root / "stolen.txt") && !fs::exists(outside / "new"),
              "nothing was read from, moved out of or made in the folder behind the link");
    }
    else
        std::cout << "SKIP: this system does not allow the test to make a symbolic link\n";

    fs::remove_all(root, ec);
    fs::remove_all(outside, ec);

    std::cout << "\n" << pass << " passed, " << fail << " failed\n";
    return fail == 0 ? 0 : 1;
}
