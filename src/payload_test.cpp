#include "payload.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fun {
namespace {
// The generator's 10000th number for the default seed, 5489, which the C++
// standard gives to check std::mt19937_64 implementations.
const std::uint64_t MT19937_64_10000TH = 9981545732273789042u;

TEST(RandomSourceTest, IsMt19937_64LeastSignificantByteFirst) {
  random_source source(std::nullopt, 5489);
  std::vector<std::uint8_t> buffer(8 * 10000);
  ASSERT_EQ(source.read(buffer.data(), buffer.size()), buffer.size());

  std::uint64_t last = 0;
  for (std::size_t i = 0; i < 8; i++)
    last |= std::uint64_t{buffer[8 * 9999 + i]} << (8 * i);
  EXPECT_EQ(last, MT19937_64_10000TH);
}

TEST(RandomSourceTest, TheSameSeedGivesTheSameBytes) {
  random_source first(std::nullopt, 42);
  random_source second(std::nullopt, 42);
  std::vector<std::uint8_t> first_bytes(1000);
  std::vector<std::uint8_t> second_bytes(1000);
  first.read(first_bytes.data(), first_bytes.size());
  second.read(second_bytes.data(), second_bytes.size());
  EXPECT_EQ(first_bytes, second_bytes);
}

TEST(RandomSourceTest, DifferentSeedsGiveDifferentBytes) {
  random_source first(std::nullopt, 42);
  random_source second(std::nullopt, 43);
  std::vector<std::uint8_t> first_bytes(1000);
  std::vector<std::uint8_t> second_bytes(1000);
  first.read(first_bytes.data(), first_bytes.size());
  second.read(second_bytes.data(), second_bytes.size());
  EXPECT_NE(first_bytes, second_bytes);
}

// The rest only check how many bytes there are.

TEST(RandomSourceTest, ReadsAsManyBytesAsAskedWithoutALimit) {
  random_source source(std::nullopt, 1);
  std::vector<std::uint8_t> buffer(1000);
  for (std::size_t max_bytes : {1, 7, 8, 9, 1000}) {
    EXPECT_EQ(source.read(buffer.data(), max_bytes), max_bytes) << max_bytes;
  }
}

TEST(RandomSourceTest, StopsAtTheLimit) {
  random_source source(10, 1);
  std::vector<std::uint8_t> buffer(8);
  EXPECT_EQ(source.read(buffer.data(), 8), 8u);
  EXPECT_EQ(source.read(buffer.data(), 8), 2u);
  EXPECT_EQ(source.read(buffer.data(), 8), 0u);
  EXPECT_EQ(source.read(buffer.data(), 8), 0u);
}

// The generator makes 8 bytes at a time, and a read that isn't a multiple of 8
// must not write past what it returns.
TEST(RandomSourceTest, WritesOnlyTheBytesItReturns) {
  for (std::size_t max_bytes = 0; max_bytes <= 17; max_bytes++) {
    random_source source(std::nullopt, 1);
    std::vector<std::uint8_t> buffer(32, 0xEE);
    std::vector<std::uint8_t> after(buffer.begin() + max_bytes, buffer.end());
    source.read(buffer.data(), max_bytes);
    EXPECT_EQ(
        std::vector<std::uint8_t>(buffer.begin() + max_bytes, buffer.end()),
        after)
        << max_bytes << " bytes";
  }
}
}  // namespace
}  // namespace fun
