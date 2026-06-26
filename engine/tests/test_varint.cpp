// Roundtrip tests for the varint codec (engine/include/joho/varint.hpp).
//
// No test framework on purpose (keeps the build dependency-free): we just assert,
// print a summary, and return non-zero on any failure so `ctest` / CI can catch it.

#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#include "joho/varint.hpp"

namespace {

int g_failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

// Encode one value, decode it back, and confirm it survives AND used the expected
// number of bytes.
void roundtrip(uint32_t v, std::size_t expected_bytes) {
    std::vector<uint8_t> buf;
    joho::put_varint(buf, v);
    check(buf.size() == expected_bytes, "encoded size matches varint_size");
    check(joho::varint_size(v) == expected_bytes, "varint_size predicts byte count");

    std::size_t pos = 0;
    const uint32_t decoded = joho::get_varint(buf.data(), pos);
    check(decoded == v, "decoded value equals original");
    check(pos == expected_bytes, "decode advanced past exactly the right bytes");
}

}  // namespace

int main() {
    // Boundary values where the byte count ticks up (2^7, 2^14, 2^21, 2^28).
    roundtrip(0, 1);
    roundtrip(1, 1);
    roundtrip(127, 1);
    roundtrip(128, 2);
    roundtrip(16383, 2);
    roundtrip(16384, 3);
    roundtrip(2097151, 3);
    roundtrip(2097152, 4);
    roundtrip(268435455, 4);
    roundtrip(268435456, 5);
    roundtrip(std::numeric_limits<uint32_t>::max(), 5);

    // A packed stream of several varints decodes back in order — this is exactly
    // how a compressed posting list is laid out.
    {
        const std::vector<uint32_t> values = {5, 3, 4, 388, 0, 1, 1000000};
        std::vector<uint8_t> buf;
        for (uint32_t v : values) joho::put_varint(buf, v);

        std::size_t pos = 0;
        for (uint32_t expected : values) {
            check(joho::get_varint(buf.data(), pos) == expected, "stream value matches");
        }
        check(pos == buf.size(), "stream fully consumed");
    }

    if (g_failures == 0) {
        std::printf("varint: ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("varint: %d FAILURE(S)\n", g_failures);
    return 1;
}
