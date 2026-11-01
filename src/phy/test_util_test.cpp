#include "test_util.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <vector>

namespace fun {
namespace {
// From the all-ones state, both taps stay 1 (giving 0) until the first 0
// reaches the second tap.
TEST(Prbs15Test, StartsWithTheKnownBits) {
  std::vector<std::uint8_t> expected = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                        0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
                                        0, 0, 0, 0, 0, 0, 1, 1, 0, 0};
  EXPECT_EQ(test::prbs_bits(32), expected);
}

TEST(Prbs15Test, HasMaximalLength) {
  test::prbs15 prbs;
  std::vector<std::uint8_t> bits(2 * 32767);
  for (std::uint8_t& b : bits) b = prbs.next_bit();

  // Repeats every 32767 bits.
  EXPECT_TRUE(
      std::equal(bits.begin(), bits.begin() + 32767, bits.begin() + 32767));

  // Every nonzero 15-bit pattern appears once per period.
  std::set<int> patterns;
  int ones = 0;
  for (int x = 0; x < 32767; x++) {
    int pattern = 0;
    for (int b = 0; b < 15; b++) pattern = (pattern << 1) | bits[x + b];
    patterns.insert(pattern);
    ones += bits[x];
  }
  EXPECT_EQ(patterns.size(), 32767u);
  EXPECT_EQ(patterns.count(0), 0u);

  // So it has one more 1 than 0.
  EXPECT_EQ(ones, 16384);
}

TEST(Prbs15Test, OffsetSkipsIntoTheSequence) {
  std::vector<std::uint8_t> bits = test::prbs_bits(200);
  EXPECT_EQ(test::prbs_bits(100, 100),
            std::vector<std::uint8_t>(bits.begin() + 100, bits.end()));
}

TEST(Prbs15Test, PacksBytesMsbFirst) {
  // Bits 8 to 31 of the sequence: 00000010 00000000 00001100.
  EXPECT_EQ(test::prbs_bytes(3, 8),
            std::vector<std::uint8_t>({0x02, 0x00, 0x0C}));
}
}  // namespace
}  // namespace fun
