#include "parity.h"

#include <gtest/gtest.h>

#include <bit>

namespace fun {
namespace {
TEST(ParityTest, MatchesKnownValues) {
  EXPECT_EQ(parity(0), 0);
  EXPECT_EQ(parity(1), 1);
  EXPECT_EQ(parity(0xFF), 0);
  EXPECT_EQ(parity(0x80000000), 1);
  EXPECT_EQ(parity(0xFFFFFFFF), 0);
}

// Every 16-bit value, at the bottom, middle and top of the word.
TEST(ParityTest, MatchesPopcount) {
  for (unsigned int x = 0; x < 0x10000; x++) {
    for (unsigned int value : {x, x << 8, x << 16}) {
      ASSERT_EQ(parity(value), std::popcount(value) & 1) << "value " << value;
    }
  }
}
}  // namespace
}  // namespace fun
