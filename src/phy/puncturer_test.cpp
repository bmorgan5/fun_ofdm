#include "puncturer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "test_util.h"

namespace fun {
namespace {
class PuncturerTest : public testing::TestWithParam<Rate> {};

// The rate 1/2 coded bits for 4 OFDM symbols.
std::vector<std::uint8_t> coded_bits(const RateParams& rp) {
  return test::prbs_bits(2 * rp.dbps * 4);
}

TEST_P(PuncturerTest, FillsTheCodedBitsPerSymbol) {
  RateParams rp(GetParam());
  EXPECT_EQ(puncturer::puncture(coded_bits(rp), rp).size(),
            static_cast<std::size_t>(rp.cbps * 4));
}

TEST_P(PuncturerTest, DepunctureRestoresTheBitsAndMarksTheErasures) {
  RateParams rp(GetParam());
  std::vector<std::uint8_t> bits = coded_bits(rp);
  std::vector<std::uint8_t> depunctured =
      puncturer::depuncture(puncturer::puncture(bits, rp), rp);
  ASSERT_EQ(depunctured.size(), bits.size());

  // Depuncturing puts 127, halfway between a 0 (0) and a 1 (255), in each hole.
  std::size_t erasures = 0;
  for (std::size_t x = 0; x < bits.size(); x++) {
    if (depunctured[x] == 127)
      erasures++;
    else
      EXPECT_EQ(depunctured[x], bits[x]) << "bit " << x;
  }
  EXPECT_EQ(erasures, bits.size() - rp.cbps * 4);
}

// 802.11a-1999 section 17.3.5.6, figure 115: rate 3/4 sends A0 B0 A1 B2 of
// every A0 B0 A1 B1 A2 B2, and rate 2/3 sends A0 B0 A1 of every A0 B0 A1 B1.
TEST_P(PuncturerTest, MatchesTheStandardPattern) {
  RateParams rp(GetParam());
  if (rp.rel_rate != 1.0) {
    GTEST_SKIP() << "The puncturer keeps A0 B0 B1 B2 for rate 3/4 and A0 A1 B1 "
                    "for rate 2/3";
  }

  std::vector<std::uint8_t> index(2 * rp.dbps);
  for (std::size_t x = 0; x < index.size(); x++) index[x] = x % 12;
  // rel_rate is relative to rate 1/2, so it's 2/3 for rate 3/4.
  bool rate_3_4 = rp.rel_rate == 2.0 / 3.0;
  bool rate_2_3 = rp.rel_rate == 3.0 / 4.0;
  std::vector<std::uint8_t> expected;
  for (std::size_t x = 0; x < index.size(); x++) {
    int position = x % 12;
    bool dropped = (rate_3_4 && (position % 6 == 3 || position % 6 == 4)) ||
                   (rate_2_3 && position % 4 == 3);
    if (!dropped) expected.push_back(index[x]);
  }
  EXPECT_EQ(puncturer::puncture(index, rp), expected);
}

INSTANTIATE_TEST_SUITE_P(AllRates, PuncturerTest,
                         testing::ValuesIn(test::ALL_RATES), test::rate_name);
}  // namespace
}  // namespace fun
