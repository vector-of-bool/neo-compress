#include <neo/crc32.hpp>

#include <catch2/catch.hpp>

TEST_CASE("CRC-32 some data") {
    CHECK(neo::crc32::calc(neo::const_buffer("The quick brown fox jumps over the lazy dog"))
          == 0x414FA339);
}
