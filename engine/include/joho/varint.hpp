#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Variable-length integer (varint / LEB128) codec — the building block of
// posting-list compression.
//
// Idea: a uint32 is 4 fixed bytes, but most numbers we store are small (especially
// after delta-encoding a sorted posting list). A varint spends only as many bytes
// as a value needs:
//   * 7 data bits per byte; the high bit (0x80) is a "continuation" flag.
//   * value < 2^7  (128)        -> 1 byte
//   * value < 2^14 (16384)      -> 2 bytes
//   * value < 2^21              -> 3 bytes
//   * ... up to 5 bytes for the full uint32 range.
//
// Header-only and `inline` because these are tiny, extremely hot functions called
// once per posting — we want the compiler to inline them into the decode loop.

namespace joho {

// Append the varint encoding of `v` to `out`.
inline void put_varint(std::vector<uint8_t>& out, uint32_t v) {
    // Emit 7 bits at a time, low bits first. Set the continuation bit (0x80) on
    // every byte except the last.
    while (v >= 0x80) {
        out.push_back(static_cast<uint8_t>(v) | 0x80);
        v >>= 7;
    }
    out.push_back(static_cast<uint8_t>(v));
}

// Decode one varint from `data` starting at `pos`; advance `pos` past the bytes
// consumed and return the value. The caller guarantees `pos` is in bounds (the
// stream we write is always well-formed).
inline uint32_t get_varint(const uint8_t* data, std::size_t& pos) {
    uint32_t result = 0;
    int shift = 0;
    while (true) {
        const uint8_t byte = data[pos++];
        result |= static_cast<uint32_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) break;  // high bit clear => last byte
        shift += 7;
    }
    return result;
}

// How many bytes the varint encoding of `v` would occupy (handy for pre-sizing
// buffers / reporting). Mirrors the loop in put_varint.
inline std::size_t varint_size(uint32_t v) {
    std::size_t n = 1;
    while (v >= 0x80) { v >>= 7; ++n; }
    return n;
}

}  // namespace joho
