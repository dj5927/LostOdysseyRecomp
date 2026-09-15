#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace install::crypto
{

// ----------------------------------------------------------------------------
// SHA-256
// ----------------------------------------------------------------------------
using Sha256Digest = std::array<uint8_t, 32>;

class Sha256
{
public:
    Sha256() { Reset(); }

    void Reset()
    {
        state_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                  0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        totalBits_ = 0;
        bufferLen_ = 0;
    }

    void Update(const void* data, size_t len)
    {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        totalBits_ += static_cast<uint64_t>(len) * 8;

        if (bufferLen_ > 0)
        {
            size_t take = std::min(len, 64 - bufferLen_);
            std::copy(p, p + take, buffer_.begin() + bufferLen_);
            bufferLen_ += take;
            p += take;
            len -= take;
            if (bufferLen_ == 64)
            {
                ProcessBlock(buffer_.data());
                bufferLen_ = 0;
            }
        }

        while (len >= 64)
        {
            ProcessBlock(p);
            p += 64;
            len -= 64;
        }

        if (len > 0)
        {
            std::copy(p, p + len, buffer_.begin());
            bufferLen_ = len;
        }
    }

    Sha256Digest Finalize()
    {
        // Padding: 0x80, zeros, 64-bit big-endian length
        buffer_[bufferLen_++] = 0x80;
        if (bufferLen_ > 56)
        {
            std::fill(buffer_.begin() + bufferLen_, buffer_.end(), 0);
            ProcessBlock(buffer_.data());
            bufferLen_ = 0;
        }
        std::fill(buffer_.begin() + bufferLen_, buffer_.begin() + 56, 0);
        for (size_t i = 0; i < 8; ++i)
        {
            buffer_[63 - i] = static_cast<uint8_t>(totalBits_ >> (i * 8));
        }
        ProcessBlock(buffer_.data());

        Sha256Digest digest{};
        for (size_t i = 0; i < 32; ++i)
        {
            digest[i] = static_cast<uint8_t>(state_[i / 4] >> (24 - (i % 4) * 8));
        }
        return digest;
    }

private:
    static inline uint32_t Rotr(uint32_t x, uint32_t n)
    {
        return (x >> n) | (x << (32 - n));
    }

    void ProcessBlock(const uint8_t* p)
    {
        static constexpr uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };

        uint32_t w[64];
        for (size_t i = 0; i < 16; ++i)
        {
            w[i] = (static_cast<uint32_t>(p[i * 4]) << 24) |
                   (static_cast<uint32_t>(p[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(p[i * 4 + 2]) << 8) |
                   static_cast<uint32_t>(p[i * 4 + 3]);
        }
        for (size_t i = 16; i < 64; ++i)
        {
            uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = state_[0];
        uint32_t b = state_[1];
        uint32_t c = state_[2];
        uint32_t d = state_[3];
        uint32_t e = state_[4];
        uint32_t f = state_[5];
        uint32_t g = state_[6];
        uint32_t h = state_[7];

        for (size_t i = 0; i < 64; ++i)
        {
            uint32_t S1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t temp1 = h + S1 + ch + K[i] + w[i];
            uint32_t S0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<uint32_t, 8> state_{};
    std::array<uint8_t, 64> buffer_{};
    uint64_t totalBits_ = 0;
    size_t bufferLen_ = 0;
};

inline Sha256Digest ComputeSha256(const void* data, size_t len)
{
    Sha256 ctx;
    ctx.Update(data, len);
    return ctx.Finalize();
}

inline std::string HexString(const uint8_t* data, size_t len)
{
    static constexpr char hexChars[] = "0123456789abcdef";
    std::string result;
    result.reserve(len * 2);
    for (size_t i = 0; i < len; ++i)
    {
        uint8_t b = data[i];
        result.push_back(hexChars[b >> 4]);
        result.push_back(hexChars[b & 0xf]);
    }
    return result;
}

template <size_t N>
inline std::string HexString(const std::array<uint8_t, N>& bytes)
{
    return HexString(bytes.data(), bytes.size());
}

inline std::string Sha256Hex(const void* data, size_t len)
{
    auto d = ComputeSha256(data, len);
    return HexString(d);
}

// ----------------------------------------------------------------------------
// SHA-1
// ----------------------------------------------------------------------------
using Sha1Digest = std::array<uint8_t, 20>;

class Sha1
{
public:
    Sha1() { Reset(); }

    void Reset()
    {
        state_[0] = 0x67452301;
        state_[1] = 0xEFCDAB89;
        state_[2] = 0x98BADCFE;
        state_[3] = 0x10325476;
        state_[4] = 0xC3D2E1F0;
        totalBits_ = 0;
        bufferLen_ = 0;
    }

    void Update(const void* data, size_t len)
    {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        totalBits_ += static_cast<uint64_t>(len) * 8;

        if (bufferLen_ > 0)
        {
            size_t take = std::min(len, 64 - bufferLen_);
            std::copy(p, p + take, buffer_.begin() + bufferLen_);
            bufferLen_ += take;
            p += take;
            len -= take;
            if (bufferLen_ == 64)
            {
                ProcessBlock(buffer_.data());
                bufferLen_ = 0;
            }
        }

        while (len >= 64)
        {
            ProcessBlock(p);
            p += 64;
            len -= 64;
        }

        if (len > 0)
        {
            std::copy(p, p + len, buffer_.begin());
            bufferLen_ = len;
        }
    }

    Sha1Digest Finalize()
    {
        buffer_[bufferLen_++] = 0x80;
        if (bufferLen_ > 56)
        {
            std::fill(buffer_.begin() + bufferLen_, buffer_.end(), 0);
            ProcessBlock(buffer_.data());
            bufferLen_ = 0;
        }
        std::fill(buffer_.begin() + bufferLen_, buffer_.begin() + 56, 0);
        for (size_t i = 0; i < 8; ++i)
        {
            buffer_[63 - i] = static_cast<uint8_t>(totalBits_ >> (i * 8));
        }
        ProcessBlock(buffer_.data());

        Sha1Digest digest{};
        for (size_t i = 0; i < 20; ++i)
        {
            digest[i] = static_cast<uint8_t>(state_[i / 4] >> (24 - (i % 4) * 8));
        }
        return digest;
    }

private:
    static inline uint32_t LeftRotate(uint32_t value, size_t count)
    {
        return (value << count) ^ (value >> (32 - count));
    }

    void ProcessBlock(const uint8_t* p)
    {
        uint32_t w[80];
        for (size_t i = 0; i < 16; ++i)
        {
            w[i] = (static_cast<uint32_t>(p[i * 4]) << 24) |
                   (static_cast<uint32_t>(p[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(p[i * 4 + 2]) << 8) |
                   static_cast<uint32_t>(p[i * 4 + 3]);
        }
        for (size_t i = 16; i < 80; ++i)
        {
            w[i] = LeftRotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = state_[0];
        uint32_t b = state_[1];
        uint32_t c = state_[2];
        uint32_t d = state_[3];
        uint32_t e = state_[4];

        for (size_t i = 0; i < 80; ++i)
        {
            uint32_t f = 0, k = 0;
            if (i < 20)
            {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            }
            else if (i < 40)
            {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            }
            else if (i < 60)
            {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            }
            else
            {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }

            uint32_t temp = LeftRotate(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = LeftRotate(b, 30);
            b = a;
            a = temp;
        }

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
    }

    std::array<uint32_t, 5> state_{};
    std::array<uint8_t, 64> buffer_{};
    uint64_t totalBits_ = 0;
    size_t bufferLen_ = 0;
};

inline Sha1Digest ComputeSha1(const void* data, size_t len)
{
    Sha1 ctx;
    ctx.Update(data, len);
    return ctx.Finalize();
}

inline std::string Sha1Hex(const void* data, size_t len)
{
    auto d = ComputeSha1(data, len);
    return HexString(d);
}

// ----------------------------------------------------------------------------
// MD5 (Portable implementation so non-Windows platforms don't return empty)
// ----------------------------------------------------------------------------
using Md5Digest = std::array<uint8_t, 16>;

class Md5
{
public:
    Md5() { Reset(); }

    void Reset()
    {
        state_[0] = 0x67452301;
        state_[1] = 0xefcdab89;
        state_[2] = 0x98badcfe;
        state_[3] = 0x10325476;
        totalBits_ = 0;
        bufferLen_ = 0;
    }

    void Update(const void* data, size_t len)
    {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        totalBits_ += static_cast<uint64_t>(len) * 8;

        if (bufferLen_ > 0)
        {
            size_t take = std::min(len, 64 - bufferLen_);
            std::copy(p, p + take, buffer_.begin() + bufferLen_);
            bufferLen_ += take;
            p += take;
            len -= take;
            if (bufferLen_ == 64)
            {
                ProcessBlock(buffer_.data());
                bufferLen_ = 0;
            }
        }

        while (len >= 64)
        {
            ProcessBlock(p);
            p += 64;
            len -= 64;
        }

        if (len > 0)
        {
            std::copy(p, p + len, buffer_.begin());
            bufferLen_ = len;
        }
    }

    Md5Digest Finalize()
    {
        buffer_[bufferLen_++] = 0x80;
        if (bufferLen_ > 56)
        {
            std::fill(buffer_.begin() + bufferLen_, buffer_.end(), 0);
            ProcessBlock(buffer_.data());
            bufferLen_ = 0;
        }
        std::fill(buffer_.begin() + bufferLen_, buffer_.begin() + 56, 0);
        for (size_t i = 0; i < 8; ++i)
        {
            buffer_[56 + i] = static_cast<uint8_t>(totalBits_ >> (i * 8));
        }
        ProcessBlock(buffer_.data());

        Md5Digest digest{};
        for (size_t i = 0; i < 16; ++i)
        {
            digest[i] = static_cast<uint8_t>(state_[i / 4] >> ((i % 4) * 8));
        }
        return digest;
    }

private:
    static inline uint32_t F(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (~x & z); }
    static inline uint32_t G(uint32_t x, uint32_t y, uint32_t z) { return (x & z) | (y & ~z); }
    static inline uint32_t H(uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; }
    static inline uint32_t I(uint32_t x, uint32_t y, uint32_t z) { return y ^ (x | ~z); }
    static inline uint32_t Rotl(uint32_t x, uint32_t n) { return (x << n) | (x >> (32 - n)); }

    void ProcessBlock(const uint8_t* p)
    {
        static constexpr uint32_t K[64] = {
            0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
            0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
            0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
            0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
            0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
            0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
            0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc1a, 0xffeff47d, 0x85845dd1,
            0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
        };

        static constexpr uint32_t S[64] = {
            7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,
            5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,
            4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,
            6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21
        };

        uint32_t m[16];
        for (size_t i = 0; i < 16; ++i)
        {
            m[i] = static_cast<uint32_t>(p[i * 4]) |
                   (static_cast<uint32_t>(p[i * 4 + 1]) << 8) |
                   (static_cast<uint32_t>(p[i * 4 + 2]) << 16) |
                   (static_cast<uint32_t>(p[i * 4 + 3]) << 24);
        }

        uint32_t a = state_[0];
        uint32_t b = state_[1];
        uint32_t c = state_[2];
        uint32_t d = state_[3];

        for (size_t i = 0; i < 64; ++i)
        {
            uint32_t f = 0, g = 0;
            if (i < 16)
            {
                f = F(b, c, d);
                g = i;
            }
            else if (i < 32)
            {
                f = G(b, c, d);
                g = (5 * i + 1) % 16;
            }
            else if (i < 48)
            {
                f = H(b, c, d);
                g = (3 * i + 5) % 16;
            }
            else
            {
                f = I(b, c, d);
                g = (7 * i) % 16;
            }

            uint32_t temp = d;
            d = c;
            c = b;
            b = b + Rotl(a + f + K[i] + m[g], S[i]);
            a = temp;
        }

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
    }

    std::array<uint32_t, 4> state_{};
    std::array<uint8_t, 64> buffer_{};
    uint64_t totalBits_ = 0;
    size_t bufferLen_ = 0;
};

inline Md5Digest ComputeMd5(const void* data, size_t len)
{
    Md5 ctx;
    ctx.Update(data, len);
    return ctx.Finalize();
}

inline std::string Md5Hex(const void* data, size_t len)
{
    auto d = ComputeMd5(data, len);
    return HexString(d);
}

} // namespace install::crypto
