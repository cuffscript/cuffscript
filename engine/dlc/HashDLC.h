#pragma once

#include "DLCCommon.h"

#include <array>
#include <cstdint>

// DLC:hash: one-way hashes and checksums, message authentication, key derivation, and the encodings that write bytes
// as text. Every function works on bytes: a str gives its bytes as they are (the UTF-8 of its text), and a list of
// whole numbers from 0 to 255 gives those bytes. Nothing here depends on the platform, and nothing is a secret kept
// by the engine.

namespace cuff
{

    namespace hashing
    {

        // ---- the hash functions ------------------------------------------------------------------------------------
        //
        // Each is a small object fed with update() and closed with digest(); they follow FIPS 180-4 (SHA-1, SHA-256,
        // SHA-512) and RFC 1321 (MD5) word for word, so their tests are the published test vectors.

        inline uint32_t rotr32(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
        inline uint32_t rotl32(uint32_t x, unsigned n) { return (x << n) | (x >> (32 - n)); }
        inline uint64_t rotr64(uint64_t x, unsigned n) { return (x >> n) | (x << (64 - n)); }

        // What the four hashes share: a block buffer, a running length, and padding with the length at the end.
        template <size_t BlockBytes, typename Derived>
        class BlockHash
        {
        public:
            CUFF_NOINLINE void update(const unsigned char *data, size_t n)
            {
                total_ += n;
                while (n > 0)
                {
                    const size_t take = std::min(n, BlockBytes - used_);
                    std::memcpy(buffer_ + used_, data, take);
                    used_ += take;
                    data += take;
                    n -= take;
                    if (used_ == BlockBytes)
                    {
                        static_cast<Derived *>(this)->block(buffer_);
                        used_ = 0;
                    }
                }
            }
            void update(const std::string &s) { update(reinterpret_cast<const unsigned char *>(s.data()), s.size()); }

        protected:
            // Appends 0x80, zeros, and the length in bits (lengthBytes wide, big- or little-endian).
            void pad(size_t lengthBytes, bool bigEndian)
            {
                const uint64_t bits = static_cast<uint64_t>(total_) * 8;
                const unsigned char one = 0x80;
                update(&one, 1);
                const unsigned char zero = 0;
                while (used_ != BlockBytes - lengthBytes)
                    update(&zero, 1);
                unsigned char len[16] = {};
                for (size_t k = 0; k < 8; ++k)
                {
                    const unsigned char byte = static_cast<unsigned char>(bits >> (8 * k));
                    if (bigEndian)
                        len[lengthBytes - 1 - k] = byte;
                    else
                        len[k] = byte;
                }
                update(len, lengthBytes);
            }

            unsigned char buffer_[BlockBytes] = {};
            size_t used_ = 0;
            size_t total_ = 0;
        };

        class Sha256 : public BlockHash<64, Sha256>
        {
        public:
            static constexpr size_t kDigestBytes = 32;
            static constexpr size_t kBlockBytes = 64;

            void block(const unsigned char *p)
            {
                static constexpr uint32_t K[64] = {
                    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
                    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
                    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
                    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
                    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
                    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
                    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
                    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
                uint32_t w[64];
                for (int t = 0; t < 16; ++t)
                    w[t] = static_cast<uint32_t>(p[4 * t]) << 24 | static_cast<uint32_t>(p[4 * t + 1]) << 16 |
                           static_cast<uint32_t>(p[4 * t + 2]) << 8 | static_cast<uint32_t>(p[4 * t + 3]);
                for (int t = 16; t < 64; ++t)
                {
                    const uint32_t s0 = rotr32(w[t - 15], 7) ^ rotr32(w[t - 15], 18) ^ (w[t - 15] >> 3);
                    const uint32_t s1 = rotr32(w[t - 2], 17) ^ rotr32(w[t - 2], 19) ^ (w[t - 2] >> 10);
                    w[t] = w[t - 16] + s0 + w[t - 7] + s1;
                }
                uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
                for (int t = 0; t < 64; ++t)
                {
                    const uint32_t t1 = h + (rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25)) + ((e & f) ^ (~e & g)) + K[t] + w[t];
                    const uint32_t t2 = (rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
                    h = g;
                    g = f;
                    f = e;
                    e = d + t1;
                    d = c;
                    c = b;
                    b = a;
                    a = t1 + t2;
                }
                h_[0] += a, h_[1] += b, h_[2] += c, h_[3] += d, h_[4] += e, h_[5] += f, h_[6] += g, h_[7] += h;
            }

            std::string digest()
            {
                pad(8, true);
                std::string out(kDigestBytes, '\0');
                for (size_t k = 0; k < 8; ++k)
                    for (size_t j = 0; j < 4; ++j)
                        out[4 * k + j] = static_cast<char>(h_[k] >> (24 - 8 * j));
                return out;
            }

        private:
            uint32_t h_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        };

        class Sha512 : public BlockHash<128, Sha512>
        {
        public:
            static constexpr size_t kDigestBytes = 64;
            static constexpr size_t kBlockBytes = 128;

            void block(const unsigned char *p)
            {
                static constexpr uint64_t K[80] = {
                    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL,
                    0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
                    0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL, 0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
                    0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
                    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL, 0x983e5152ee66dfabULL,
                    0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
                    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL,
                    0x53380d139d95b3dfULL, 0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
                    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
                    0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL, 0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
                    0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL,
                    0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
                    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL, 0xca273eceea26619cULL,
                    0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
                    0x113f9804bef90daeULL, 0x1b710b35131c471bULL, 0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
                    0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};
                uint64_t w[80];
                for (int t = 0; t < 16; ++t)
                {
                    uint64_t v = 0;
                    for (int j = 0; j < 8; ++j)
                        v = v << 8 | p[8 * t + j];
                    w[t] = v;
                }
                for (int t = 16; t < 80; ++t)
                {
                    const uint64_t s0 = rotr64(w[t - 15], 1) ^ rotr64(w[t - 15], 8) ^ (w[t - 15] >> 7);
                    const uint64_t s1 = rotr64(w[t - 2], 19) ^ rotr64(w[t - 2], 61) ^ (w[t - 2] >> 6);
                    w[t] = w[t - 16] + s0 + w[t - 7] + s1;
                }
                uint64_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
                for (int t = 0; t < 80; ++t)
                {
                    const uint64_t t1 = h + (rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41)) + ((e & f) ^ (~e & g)) + K[t] + w[t];
                    const uint64_t t2 = (rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39)) + ((a & b) ^ (a & c) ^ (b & c));
                    h = g;
                    g = f;
                    f = e;
                    e = d + t1;
                    d = c;
                    c = b;
                    b = a;
                    a = t1 + t2;
                }
                h_[0] += a, h_[1] += b, h_[2] += c, h_[3] += d, h_[4] += e, h_[5] += f, h_[6] += g, h_[7] += h;
            }

            std::string digest()
            {
                pad(16, true);
                std::string out(kDigestBytes, '\0');
                for (size_t k = 0; k < 8; ++k)
                    for (size_t j = 0; j < 8; ++j)
                        out[8 * k + j] = static_cast<char>(h_[k] >> (56 - 8 * j));
                return out;
            }

        private:
            uint64_t h_[8] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
                              0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
        };

        class Sha1 : public BlockHash<64, Sha1>
        {
        public:
            static constexpr size_t kDigestBytes = 20;

            void block(const unsigned char *p)
            {
                uint32_t w[80];
                for (int t = 0; t < 16; ++t)
                    w[t] = static_cast<uint32_t>(p[4 * t]) << 24 | static_cast<uint32_t>(p[4 * t + 1]) << 16 |
                           static_cast<uint32_t>(p[4 * t + 2]) << 8 | static_cast<uint32_t>(p[4 * t + 3]);
                for (int t = 16; t < 80; ++t)
                    w[t] = rotl32(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
                uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4];
                for (int t = 0; t < 80; ++t)
                {
                    uint32_t f, k;
                    if (t < 20)
                        f = (b & c) | (~b & d), k = 0x5a827999;
                    else if (t < 40)
                        f = b ^ c ^ d, k = 0x6ed9eba1;
                    else if (t < 60)
                        f = (b & c) | (b & d) | (c & d), k = 0x8f1bbcdc;
                    else
                        f = b ^ c ^ d, k = 0xca62c1d6;
                    const uint32_t temp = rotl32(a, 5) + f + e + k + w[t];
                    e = d;
                    d = c;
                    c = rotl32(b, 30);
                    b = a;
                    a = temp;
                }
                h_[0] += a, h_[1] += b, h_[2] += c, h_[3] += d, h_[4] += e;
            }

            std::string digest()
            {
                pad(8, true);
                std::string out(kDigestBytes, '\0');
                for (size_t k = 0; k < 5; ++k)
                    for (size_t j = 0; j < 4; ++j)
                        out[4 * k + j] = static_cast<char>(h_[k] >> (24 - 8 * j));
                return out;
            }

        private:
            uint32_t h_[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
        };

        class Md5 : public BlockHash<64, Md5>
        {
        public:
            static constexpr size_t kDigestBytes = 16;

            void block(const unsigned char *p)
            {
                static constexpr uint32_t K[64] = {
                    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
                    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
                    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
                    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
                    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
                    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
                    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
                    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
                static constexpr unsigned S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                                   5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
                                                   4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                                   6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
                uint32_t m[16];
                for (int t = 0; t < 16; ++t)
                    m[t] = static_cast<uint32_t>(p[4 * t]) | static_cast<uint32_t>(p[4 * t + 1]) << 8 |
                           static_cast<uint32_t>(p[4 * t + 2]) << 16 | static_cast<uint32_t>(p[4 * t + 3]) << 24;
                uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
                for (int i = 0; i < 64; ++i)
                {
                    uint32_t f;
                    int g;
                    if (i < 16)
                        f = (b & c) | (~b & d), g = i;
                    else if (i < 32)
                        f = (d & b) | (~d & c), g = (5 * i + 1) % 16;
                    else if (i < 48)
                        f = b ^ c ^ d, g = (3 * i + 5) % 16;
                    else
                        f = c ^ (b | ~d), g = (7 * i) % 16;
                    const uint32_t next = d;
                    d = c;
                    c = b;
                    b = b + rotl32(a + f + K[i] + m[g], S[i]);
                    a = next;
                }
                h_[0] += a, h_[1] += b, h_[2] += c, h_[3] += d;
            }

            std::string digest()
            {
                pad(8, false);
                std::string out(kDigestBytes, '\0');
                for (size_t k = 0; k < 4; ++k)
                    for (size_t j = 0; j < 4; ++j)
                        out[4 * k + j] = static_cast<char>(h_[k] >> (8 * j));
                return out;
            }

        private:
            uint32_t h_[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
        };

        template <typename H>
        CUFF_NOINLINE std::string digestOf(const std::string &data)
        {
            H h;
            h.update(data);
            return h.digest();
        }

        // CRC-32 as zip, PNG and Ethernet use it (reflected, polynomial 0xEDB88320).
        CUFF_NOINLINE inline uint32_t crc32(const std::string &data)
        {
            static const std::array<uint32_t, 256> table = []
            {
                std::array<uint32_t, 256> t{};
                for (uint32_t n = 0; n < 256; ++n)
                {
                    uint32_t c = n;
                    for (int k = 0; k < 8; ++k)
                        c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                    t[n] = c;
                }
                return t;
            }();
            uint32_t c = 0xFFFFFFFFu;
            for (unsigned char byte : data)
                c = table[(c ^ byte) & 0xFF] ^ (c >> 8);
            return c ^ 0xFFFFFFFFu;
        }

        // HMAC (RFC 2104) over one of the hashes above.
        template <typename H>
        CUFF_NOINLINE std::string hmac(std::string key, const std::string &message)
        {
            constexpr size_t B = H::kBlockBytes;
            if (key.size() > B)
                key = digestOf<H>(key);
            key.resize(B, '\0');
            std::string inner(B, '\0'), outer(B, '\0');
            for (size_t k = 0; k < B; ++k)
            {
                inner[k] = static_cast<char>(key[k] ^ 0x36);
                outer[k] = static_cast<char>(key[k] ^ 0x5c);
            }
            H in;
            in.update(inner);
            in.update(message);
            H out;
            out.update(outer);
            out.update(in.digest());
            return out.digest();
        }

        // PBKDF2 (RFC 8018, section 5.2) with HMAC-SHA-256. The keyed hash of the password is set up once and copied
        // for every round, which halves the work of a naive loop.
        CUFF_NOINLINE inline std::string pbkdf2Sha256(std::string password, const std::string &salt, uint64_t iterations, size_t length)
        {
            constexpr size_t B = Sha256::kBlockBytes;
            if (password.size() > B)
                password = digestOf<Sha256>(password);
            password.resize(B, '\0');
            std::string ipad(B, '\0'), opad(B, '\0');
            for (size_t k = 0; k < B; ++k)
            {
                ipad[k] = static_cast<char>(password[k] ^ 0x36);
                opad[k] = static_cast<char>(password[k] ^ 0x5c);
            }
            Sha256 innerStart, outerStart;
            innerStart.update(ipad);
            outerStart.update(opad);
            auto mac = [&](const std::string &message)
            {
                Sha256 in = innerStart;
                in.update(message);
                Sha256 out = outerStart;
                out.update(in.digest());
                return out.digest();
            };
            std::string result;
            for (uint32_t blockIndex = 1; result.size() < length; ++blockIndex)
            {
                std::string first = salt;
                for (int k = 3; k >= 0; --k)
                    first += static_cast<char>(blockIndex >> (8 * k));
                std::string u = mac(first);
                std::string t = u;
                for (uint64_t i = 1; i < iterations; ++i)
                {
                    u = mac(u);
                    for (size_t k = 0; k < t.size(); ++k)
                        t[k] = static_cast<char>(t[k] ^ u[k]);
                }
                result += t;
            }
            result.resize(length);
            return result;
        }

        // ---- encodings -------------------------------------------------------------------------------------------

        CUFF_NOINLINE inline std::string hexEncode(const std::string &bytes)
        {
            static const char digits[] = "0123456789abcdef";
            std::string out;
            out.reserve(bytes.size() * 2);
            for (unsigned char c : bytes)
            {
                out += digits[c >> 4];
                out += digits[c & 15];
            }
            return out;
        }

        inline int hexValue(char c)
        {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        }

        inline const char *base64Alphabet(bool url)
        {
            return url ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
                       : "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        }

        // RFC 4648, sections 4 and 5. The URL-safe form is written without padding, as most of its uses (JSON Web
        // Tokens, URLs) want it.
        CUFF_NOINLINE inline std::string base64Encode(const std::string &bytes, bool url)
        {
            const char *alphabet = base64Alphabet(url);
            std::string out;
            out.reserve((bytes.size() + 2) / 3 * 4);
            size_t k = 0;
            for (; k + 2 < bytes.size(); k += 3)
            {
                const uint32_t v = static_cast<uint32_t>(static_cast<unsigned char>(bytes[k])) << 16 |
                                   static_cast<uint32_t>(static_cast<unsigned char>(bytes[k + 1])) << 8 |
                                   static_cast<unsigned char>(bytes[k + 2]);
                out += alphabet[v >> 18];
                out += alphabet[(v >> 12) & 63];
                out += alphabet[(v >> 6) & 63];
                out += alphabet[v & 63];
            }
            const size_t rest = bytes.size() - k;
            if (rest > 0)
            {
                uint32_t v = static_cast<uint32_t>(static_cast<unsigned char>(bytes[k])) << 16;
                if (rest == 2)
                    v |= static_cast<uint32_t>(static_cast<unsigned char>(bytes[k + 1])) << 8;
                out += alphabet[v >> 18];
                out += alphabet[(v >> 12) & 63];
                if (rest == 2)
                    out += alphabet[(v >> 6) & 63];
                if (!url)
                    out += rest == 2 ? "=" : "==";
            }
            return out;
        }

        // False for text that is not of the alphabet. Padding may be left out; when it is written it must be right.
        CUFF_NOINLINE inline bool base64Decode(const std::string &text, bool url, std::string &out, std::string &problem)
        {
            int value[256];
            std::fill(std::begin(value), std::end(value), -1);
            const char *alphabet = base64Alphabet(url);
            for (int k = 0; k < 64; ++k)
                value[static_cast<unsigned char>(alphabet[k])] = k;
            size_t end = text.size();
            size_t padding = 0;
            while (end > 0 && text[end - 1] == '=' && padding < 2)
                --end, ++padding;
            if (padding > 0 && text.size() % 4 != 0)
            {
                problem = "its padding does not make the length a multiple of 4";
                return false;
            }
            if (end % 4 == 1)
            {
                problem = "its length cannot be that of Base64 text";
                return false;
            }
            out.clear();
            out.reserve(end / 4 * 3 + 2);
            uint32_t acc = 0;
            int bits = 0;
            for (size_t k = 0; k < end; ++k)
            {
                const int v = value[static_cast<unsigned char>(text[k])];
                if (v < 0)
                {
                    problem = text[k] == '=' ? "'=' may only stand at the end" : "it has a character that is not of the alphabet, at position " + std::to_string(k + 1);
                    return false;
                }
                acc = (acc << 6) | static_cast<uint32_t>(v);
                bits += 6;
                if (bits >= 8)
                {
                    bits -= 8;
                    out += static_cast<char>((acc >> bits) & 0xFF);
                }
            }
            return true;
        }

    }

    namespace hashdlc
    {

        // A function's data argument: a str as its bytes, or a list of whole numbers from 0 to 255.
        CUFF_NOINLINE inline std::string bytesArg(const char *fn, std::vector<Value> &args, size_t i, const SourceLocation &loc)
        {
            const Value &v = args[i];
            if (v.isStr())
                return v.asStr();
            if (!v.isList())
                throw TypeError(std::string(fn) + "() expects argument " + std::to_string(i + 1) + " to be a str or a list of bytes, got " +
                                    valueTypeName(v.type()),
                                loc);
            const std::vector<Value> &items = v.asList()->items;
            std::string out;
            out.reserve(items.size());
            for (size_t k = 0; k < items.size(); ++k)
            {
                const Value &item = items[k];
                if (!item.isNumber() || item.asNumber() < 0 || item.asNumber() > 255 || item.asNumber() != std::floor(item.asNumber()))
                    throw ValueError(std::string(fn) + "() expects argument " + std::to_string(i + 1) +
                                         " to hold bytes, whole numbers from 0 to 255; item " + std::to_string(k + 1) + " is " +
                                         item.toDisplayString(),
                                     loc);
                out += static_cast<char>(static_cast<unsigned char>(item.asNumber()));
            }
            return out;
        }

        inline Value hexValueOf(const std::string &bytes) { return Value::makeStr(hashing::hexEncode(bytes)); }

        inline Value base64EncodeCall(const char *fn, std::vector<Value> &args, bool url, const SourceLocation &loc)
        {
            expectArgCount(fn, args, 1, loc);
            const std::string bytes = bytesArg(fn, args, 0, loc);
            ensureStringSize((bytes.size() + 2) / 3 * 4, loc);
            return Value::makeStr(hashing::base64Encode(bytes, url));
        }

        inline Value base64DecodeCall(const char *fn, std::vector<Value> &args, bool url, const SourceLocation &loc)
        {
            expectArgCount(fn, args, 1, loc);
            std::string out, problem;
            if (!hashing::base64Decode(expectStr(fn, args, 0, loc), url, out, problem))
                throw ValueError(std::string(fn) + "() expects " + (url ? "URL-safe " : "") + "Base64 text, but " + problem, loc);
            return Value::makeStr(std::move(out));
        }

    }

    inline void registerHashDLC(std::unordered_map<std::string, NativeFn> &reg)
    {
        // Plain functions, not one closure type each: std::function then needs one handler for all of them, which
        // keeps the library small.
        auto add = [&reg](const char *name, Value (*fn)(std::vector<Value> &, const SourceLocation &)) { reg[name] = fn; };

        using namespace hashing;
        using hashdlc::bytesArg;

        // ---- hashes and checksums: lowercase hexadecimal ----------------------------------------------------------

        add("hash_sha256", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("hash_sha256", args, 1, loc);
            return hashdlc::hexValueOf(digestOf<Sha256>(bytesArg("hash_sha256", args, 0, loc)));
        });
        add("hash_sha512", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("hash_sha512", args, 1, loc);
            return hashdlc::hexValueOf(digestOf<Sha512>(bytesArg("hash_sha512", args, 0, loc)));
        });
        add("hash_sha1", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("hash_sha1", args, 1, loc);
            return hashdlc::hexValueOf(digestOf<Sha1>(bytesArg("hash_sha1", args, 0, loc)));
        });
        add("hash_md5", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("hash_md5", args, 1, loc);
            return hashdlc::hexValueOf(digestOf<Md5>(bytesArg("hash_md5", args, 0, loc)));
        });
        add("hash_crc32", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("hash_crc32", args, 1, loc);
            const uint32_t c = crc32(bytesArg("hash_crc32", args, 0, loc));
            std::string bytes(4, '\0');
            for (int k = 0; k < 4; ++k)
                bytes[static_cast<size_t>(k)] = static_cast<char>(c >> (24 - 8 * k));
            return hashdlc::hexValueOf(bytes);
        });

        // ---- message authentication and key derivation ------------------------------------------------------------

        add("hash_hmac_sha256", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("hash_hmac_sha256", args, 2, loc);
            return hashdlc::hexValueOf(hmac<Sha256>(bytesArg("hash_hmac_sha256", args, 0, loc), bytesArg("hash_hmac_sha256", args, 1, loc)));
        });
        add("hash_hmac_sha512", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("hash_hmac_sha512", args, 2, loc);
            return hashdlc::hexValueOf(hmac<Sha512>(bytesArg("hash_hmac_sha512", args, 0, loc), bytesArg("hash_hmac_sha512", args, 1, loc)));
        });
        add("hash_pbkdf2_sha256", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgRange("hash_pbkdf2_sha256", args, 3, 4, loc);
            const std::string password = bytesArg("hash_pbkdf2_sha256", args, 0, loc);
            const std::string salt = bytesArg("hash_pbkdf2_sha256", args, 1, loc);
            const long long iterations = expectWhole("hash_pbkdf2_sha256", args, 2, loc);
            if (iterations < 1 || iterations > static_cast<long long>(limits::kMaxPbkdf2Iterations))
                throw ValueError("hash_pbkdf2_sha256() expects from 1 to " + std::to_string(limits::kMaxPbkdf2Iterations) +
                                     " iterations, got " + std::to_string(iterations),
                                 loc);
            long long length = 32;
            if (args.size() == 4)
            {
                length = expectWhole("hash_pbkdf2_sha256", args, 3, loc);
                if (length < 1 || length > static_cast<long long>(limits::kMaxPbkdf2Bytes))
                    throw ValueError("hash_pbkdf2_sha256() expects a length from 1 to " + std::to_string(limits::kMaxPbkdf2Bytes) +
                                         " bytes, got " + std::to_string(length),
                                     loc);
            }
            return hashdlc::hexValueOf(pbkdf2Sha256(password, salt, static_cast<uint64_t>(iterations), static_cast<size_t>(length)));
        });

        // Compares in a time that depends on the lengths only, not on where the two differ: for checking a MAC or a
        // derived key that someone else sent, where an early exit would tell them how much of a guess was right.
        add("hash_equal", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("hash_equal", args, 2, loc);
            const std::string a = bytesArg("hash_equal", args, 0, loc);
            const std::string b = bytesArg("hash_equal", args, 1, loc);
            unsigned char diff = a.size() == b.size() ? 0 : 1;
            const size_t n = std::max(a.size(), b.size());
            for (size_t k = 0; k < n; ++k)
            {
                const unsigned char x = k < a.size() ? static_cast<unsigned char>(a[k]) : 0;
                const unsigned char y = k < b.size() ? static_cast<unsigned char>(b[k]) : 0;
                diff = static_cast<unsigned char>(diff | (x ^ y));
            }
            return Value::makeBool(diff == 0);
        });

        // ---- bytes as text ----------------------------------------------------------------------------------------

        add("hash_hex_encode", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("hash_hex_encode", args, 1, loc);
            const std::string bytes = bytesArg("hash_hex_encode", args, 0, loc);
            ensureStringSize(bytes.size() * 2, loc);
            return hashdlc::hexValueOf(bytes);
        });
        add("hash_hex_decode", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("hash_hex_decode", args, 1, loc);
            const std::string &text = expectStr("hash_hex_decode", args, 0, loc);
            if (text.size() % 2 != 0)
                throw ValueError("hash_hex_decode() expects an even number of hexadecimal digits, got " + std::to_string(text.size()), loc);
            std::string out(text.size() / 2, '\0');
            for (size_t k = 0; k < out.size(); ++k)
            {
                const int hi = hexValue(text[2 * k]), lo = hexValue(text[2 * k + 1]);
                if (hi < 0 || lo < 0)
                    throw ValueError("hash_hex_decode() expects hexadecimal digits; the text has another character at position " +
                                         std::to_string(2 * k + (hi < 0 ? 1 : 2)),
                                     loc);
                out[k] = static_cast<char>(hi << 4 | lo);
            }
            return Value::makeStr(std::move(out));
        });
        add("hash_base64_encode", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        { return hashdlc::base64EncodeCall("hash_base64_encode", args, false, loc); });
        add("hash_base64_decode", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        { return hashdlc::base64DecodeCall("hash_base64_decode", args, false, loc); });
        add("hash_base64url_encode", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        { return hashdlc::base64EncodeCall("hash_base64url_encode", args, true, loc); });
        add("hash_base64url_decode", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        { return hashdlc::base64DecodeCall("hash_base64url_decode", args, true, loc); });

        add("hash_to_bytes", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("hash_to_bytes", args, 1, loc);
            const std::string &text = expectStr("hash_to_bytes", args, 0, loc);
            ensureItemCount(text.size(), loc);
            auto list = std::make_shared<ValueList>();
            list->items.reserve(text.size());
            for (unsigned char c : text)
                list->items.push_back(Value::makeNumber(c));
            return Value::makeList(std::move(list));
        });
        add("hash_from_bytes", +[](std::vector<Value> &args, const SourceLocation &loc) -> Value
        {
            expectArgCount("hash_from_bytes", args, 1, loc);
            if (!args[0].isList())
                throw TypeError("hash_from_bytes() expects argument 1 to be a list, got " + valueTypeName(args[0].type()), loc);
            return Value::makeStr(bytesArg("hash_from_bytes", args, 0, loc));
        });
    }

}
