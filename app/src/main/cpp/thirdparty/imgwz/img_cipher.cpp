//  The wz string keystream: AES-256-ECB encrypting a 16-byte block made of the region
//  IV repeated four times, then each block encrypting the previous one.  MapleLib
//  starts every string back at the head of that stream, so the masks (0xAA per byte,
//  0xAAAA per UTF-16 word) fold into two tables and a string decode is one XOR.
#include "img_internal.hpp"

#include <array>
#include <cstring>

namespace img
{
namespace
{
//! The user key every wz region uses; the region IV is what differs.
constexpr std::uint8_t user_key[32] = {
    0x13, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00, 0x00, 0xB4, 0x00, 0x00, 0x00,
    0x1B, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00,
    0x33, 0x00, 0x00, 0x00, 0x52, 0x00, 0x00, 0x00,
};

constexpr int rounds = 14;

std::uint8_t gmul(std::uint8_t a, std::uint8_t b)
{
    std::uint8_t p = 0;
    for (int i = 0; i < 8; ++i) {
        if (b & 1) {
            p ^= a;
        }
        const int high = a & 0x80;
        a = static_cast<std::uint8_t>(a << 1);
        if (high) {
            a ^= 0x1B;
        }
        b = static_cast<std::uint8_t>(b >> 1);
    }
    return p;
}

//! The AES S-box, computed rather than pasted: inverse in GF(2^8) plus the affine map.
std::array<std::uint8_t, 256> const& sbox()
{
    static std::array<std::uint8_t, 256> const table = [] {
        std::array<std::uint8_t, 256> s{};
        s[0] = 0x63;
        // Walk the field with a generator so every inverse is found in one pass.
        std::uint8_t log[256]{};
        std::uint8_t exp[256]{};
        std::uint8_t x = 1;
        for (int i = 0; i < 255; ++i) {
            exp[i] = x;
            log[x] = static_cast<std::uint8_t>(i);
            x = gmul(x, 3);
        }
        for (int v = 1; v < 256; ++v) {
            const int inv = exp[(255 - log[v]) % 255];
            std::uint8_t b = static_cast<std::uint8_t>(inv);
            std::uint8_t r = b;
            for (int i = 0; i < 4; ++i) {
                b = static_cast<std::uint8_t>((b << 1) | (b >> 7));
                r ^= b;
            }
            s[v] = static_cast<std::uint8_t>(r ^ 0x63);
        }
        return s;
    }();
    return table;
}

//! Round keys for the fixed user key; 15 words per round plus the IV-derived block
//! chaining, so this is computed once and shared.
std::array<std::uint32_t, 4 * (rounds + 1)> const& round_keys()
{
    static std::array<std::uint32_t, 4 * (rounds + 1)> const rk = [] {
        std::array<std::uint32_t, 4 * (rounds + 1)> k{};
        for (int i = 0; i < 8; ++i) {
            k[i] = (std::uint32_t(user_key[4 * i]) << 24)
                   | (std::uint32_t(user_key[4 * i + 1]) << 16)
                   | (std::uint32_t(user_key[4 * i + 2]) << 8)
                   | user_key[4 * i + 3];
        }
        std::uint8_t rcon = 1;
        for (int i = 8; i < 4 * (rounds + 1); ++i) {
            std::uint32_t t = k[i - 1];
            if (i % 8 == 0) {
                auto const& s = sbox();
                // RotWord moves the first byte last, and Rcon is XORed into the new first byte.
                const std::uint32_t rot = (t << 8) | (t >> 24);
                t = (std::uint32_t(s[static_cast<std::uint8_t>(rot >> 24)]) << 24)
                    | (std::uint32_t(s[static_cast<std::uint8_t>(rot >> 16)]) << 16)
                    | (std::uint32_t(s[static_cast<std::uint8_t>(rot >> 8)]) << 8)
                    | std::uint32_t(s[static_cast<std::uint8_t>(rot)]);
                t ^= std::uint32_t(rcon) << 24;
                rcon = static_cast<std::uint8_t>(gmul(rcon, 2));
            } else if (i % 8 == 4) {
                auto const& s = sbox();
                t = (std::uint32_t(s[static_cast<std::uint8_t>(t >> 24)]) << 24)
                    | (std::uint32_t(s[static_cast<std::uint8_t>(t >> 16)]) << 16)
                    | (std::uint32_t(s[static_cast<std::uint8_t>(t >> 8)]) << 8)
                    | std::uint32_t(s[static_cast<std::uint8_t>(t)]);
            }
            k[i] = k[i - 8] ^ t;
        }
        return k;
    }();
    return rk;
}

//! One AES-256 block, in place, column-major like the specification.
void encrypt_block(std::uint8_t b[16])
{
    auto const& rk = round_keys();
    auto const& s = sbox();
    std::uint8_t t[16];

    auto add = [&](int round) {
        for (int c = 0; c < 4; ++c) {
            std::uint32_t k = rk[4 * round + c];
            b[4 * c + 0] ^= static_cast<std::uint8_t>(k >> 24);
            b[4 * c + 1] ^= static_cast<std::uint8_t>(k >> 16);
            b[4 * c + 2] ^= static_cast<std::uint8_t>(k >> 8);
            b[4 * c + 3] ^= static_cast<std::uint8_t>(k);
        }
    };

    add(0);
    for (int round = 1; round <= rounds; ++round) {
        for (int i = 0; i < 16; ++i) {
            t[i] = s[b[i]];
        }
        // Row shifts: row r moves left by r.
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                b[4 * c + r] = t[4 * ((c + r) % 4) + r];
            }
        }
        if (round != rounds) {
            for (int c = 0; c < 4; ++c) {
                std::uint8_t* a = b + 4 * c;
                const std::uint8_t a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3];
                a[0] = gmul(a0, 2) ^ gmul(a1, 3) ^ a2 ^ a3;
                a[1] = a0 ^ gmul(a1, 2) ^ gmul(a2, 3) ^ a3;
                a[2] = a0 ^ a1 ^ gmul(a2, 2) ^ gmul(a3, 3);
                a[3] = gmul(a0, 3) ^ a1 ^ a2 ^ gmul(a3, 2);
            }
        }
        add(round);
    }
}

constexpr std::size_t block_bytes = 16;
} // namespace

void cipher::ensure(std::size_t length) const
{
    if (length <= ascii.size()) {
        return;
    }
    // Grow in big steps; the tables are read once per string and never shrink.
    std::size_t units = ((length + 255) / 256) * 256;
    std::size_t blocks = (units + block_bytes - 1) / block_bytes;
    std::vector<std::uint8_t> ks(blocks * block_bytes);
    std::uint8_t b[16];
    const std::uint32_t iv = img_iv;
    for (int i = 0; i < 4; ++i) {
        b[4 * i + 0] = static_cast<std::uint8_t>(iv >> 24);
        b[4 * i + 1] = static_cast<std::uint8_t>(iv >> 16);
        b[4 * i + 2] = static_cast<std::uint8_t>(iv >> 8);
        b[4 * i + 3] = static_cast<std::uint8_t>(iv);
    }
    for (std::size_t block = 0; block < blocks; ++block) {
        encrypt_block(b);
        std::memcpy(&ks[block * block_bytes], b, block_bytes);
    }

    ascii.resize(units);
    utf16.resize(units / 2);
    for (std::size_t i = 0; i < units; ++i) {
        ascii[i] = static_cast<std::uint8_t>(ks[i] ^ ((0xAA + i) & 0xFF));
    }
    for (std::size_t i = 0; i < units / 2; ++i) {
        const std::uint16_t word = std::uint16_t(ks[2 * i] | (ks[2 * i + 1] << 8));
        utf16[i] = static_cast<std::uint16_t>(word ^ std::uint16_t((0xAAAA + i) & 0xFFFF));
    }
}

cipher const& cipher::get()
{
    static cipher instance;
    return instance;
}
} // namespace img
