#include "engine/CuffEngine.h"
#include "engine/net/HttpClient.h"
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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

    std::cout << "\n" << pass << " passed, " << fail << " failed\n";
    return fail == 0 ? 0 : 1;
}
