#include "payload.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fun {
namespace {
// The bytes are random, so these only check how many there are.

TEST(RandomSourceTest, ReadsAsManyBytesAsAskedWithoutALimit) {
  random_source source(std::nullopt);
  std::vector<std::uint8_t> buffer(1000);
  for (std::size_t max_bytes : {1, 7, 8, 9, 1000}) {
    EXPECT_EQ(source.read(buffer.data(), max_bytes), max_bytes) << max_bytes;
  }
}

TEST(RandomSourceTest, StopsAtTheLimit) {
  random_source source(10);
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
    random_source source(std::nullopt);
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
