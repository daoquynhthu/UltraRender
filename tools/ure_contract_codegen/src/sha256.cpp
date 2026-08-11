#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>

#include "sha256.hpp"

namespace ure::contract_codegen {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

}

std::string sha256_hex(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > std::numeric_limits<std::uint64_t>::max() / 8u) {
        throw std::length_error("SHA-256 input is too large");
    }
    std::array<std::uint32_t, 8> state{
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    const auto process_block = [&state](std::span<const std::uint8_t, 64> block) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t i = 0; i < 16; ++i) {
            const std::size_t base = i * 4u;
            words[i] = (static_cast<std::uint32_t>(block[base]) << 24u) |
                       (static_cast<std::uint32_t>(block[base + 1]) << 16u) |
                       (static_cast<std::uint32_t>(block[base + 2]) << 8u) |
                       static_cast<std::uint32_t>(block[base + 3]);
        }
        for (std::size_t i = 16; i < words.size(); ++i) {
            const std::uint32_t s0 = std::rotr(words[i - 15], 7) ^ std::rotr(words[i - 15], 18) ^ (words[i - 15] >> 3u);
            const std::uint32_t s1 = std::rotr(words[i - 2], 17) ^ std::rotr(words[i - 2], 19) ^ (words[i - 2] >> 10u);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }
        auto [a, b, c, d, e, f, g, h] = state;
        for (std::size_t i = 0; i < words.size(); ++i) {
            const std::uint32_t sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const std::uint32_t choose = (e & f) ^ (~e & g);
            const std::uint32_t temp1 = h + sum1 + choose + kRoundConstants[i] + words[i];
            const std::uint32_t sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    };
    const std::size_t complete_bytes = bytes.size() - (bytes.size() % 64u);
    for (std::size_t offset = 0; offset < complete_bytes; offset += 64u) {
        process_block(std::span<const std::uint8_t, 64>(bytes.data() + offset, 64));
    }
    std::array<std::uint8_t, 128> tail{};
    const std::size_t remainder = bytes.size() - complete_bytes;
    std::ranges::copy(bytes.subspan(complete_bytes), tail.begin());
    tail[remainder] = 0x80u;
    const std::size_t tail_size = remainder < 56u ? 64u : 128u;
    const std::uint64_t bit_length = static_cast<std::uint64_t>(bytes.size()) * 8u;
    for (int shift = 56; shift >= 0; shift -= 8) {
        tail[tail_size - 8u + static_cast<std::size_t>((56 - shift) / 8)] = static_cast<std::uint8_t>(bit_length >> shift);
    }
    process_block(std::span<const std::uint8_t, 64>(tail.data(), 64));
    if (tail_size == 128u) {
        process_block(std::span<const std::uint8_t, 64>(tail.data() + 64u, 64));
    }
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (std::uint32_t word : state) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            result.push_back(digits[(word >> shift) & 0x0fu]);
        }
    }
    return result;
}

}
