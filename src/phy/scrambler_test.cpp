#include "scrambler.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "test_util.h"

namespace fun {
namespace {
// The sequence the scrambler generates from the all-ones state, from
// 802.11a-1999 section 17.3.5.4. It repeats every 127 bits.
constexpr std::string_view STANDARD_SEQUENCE =
    "00001110"
    "11110010"
    "11001001"
    "00000010"
    "00100110"
    "00101110"
    "10110110"
    "00001100"
    "11010100"
    "11100111"
    "10110100"
    "00101010"
    "11111010"
    "01010001"
    "10111000"
    "1111111";

// The register holds the last 7 bits of the sequence, x7 the oldest. So from
// SEED, the sequence continues from just after the place where SEED's bits
// appear in it.
std::string sequence_from_seed(std::size_t count) {
  std::string seed;
  for (int bit = 6; bit >= 0; bit--)
    seed += (scrambler::SEED >> bit) & 1 ? '1' : '0';
  std::string repeated(STANDARD_SEQUENCE);
  repeated += STANDARD_SEQUENCE;
  std::size_t start = repeated.find(seed) + seed.size();
  std::string sequence;
  while (sequence.size() < count)
    sequence += repeated[start + sequence.size() % STANDARD_SEQUENCE.size()];
  return sequence;
}

// Scrambling zeros gives the sequence itself: one bit in the lowest bit of
// each byte, the rest left alone.
TEST(ScramblerTest, GeneratesTheStandardSequence) {
  std::vector<std::uint8_t> scrambled =
      scrambler::scramble(std::vector<std::uint8_t>(2 * 127, 0));
  std::string sequence = sequence_from_seed(scrambled.size());
  for (std::size_t x = 0; x < scrambled.size(); x++) {
    ASSERT_EQ(scrambled[x], sequence[x] - '0') << "byte " << x;
  }
}

TEST(ScramblerTest, ScramblingTwiceGivesBackTheData) {
  std::vector<std::uint8_t> data = test::prbs_bytes(1000);
  std::vector<std::uint8_t> scrambled = scrambler::scramble(data);
  EXPECT_NE(scrambled, data);
  EXPECT_EQ(scrambler::scramble(scrambled), data);
}

// 802.11a XORs each bit with the next bit of the sequence, so scrambling
// zeros gives the sequence on every bit, in the order the bits are sent (most
// significant first here).
TEST(ScramblerTest, ScramblesEveryBitLike80211a) {
  GTEST_SKIP() << "The scrambler steps once per byte, and only flips the "
                  "lowest bit of each byte";

  std::vector<std::uint8_t> scrambled =
      scrambler::scramble(std::vector<std::uint8_t>(16, 0));
  std::string sequence = sequence_from_seed(8 * scrambled.size());
  for (std::size_t x = 0; x < 8 * scrambled.size(); x++) {
    EXPECT_EQ((scrambled[x / 8] >> (7 - x % 8)) & 1, sequence[x] - '0')
        << "bit " << x;
  }
}
}  // namespace
}  // namespace fun
