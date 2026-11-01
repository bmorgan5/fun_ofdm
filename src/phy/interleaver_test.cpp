#include "interleaver.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

#include "test_util.h"

namespace fun {
namespace {
TEST(InterleaverTest, DeinterleaveUndoesInterleave) {
  for (int symbols : {1, 2, 10}) {
    std::vector<std::uint8_t> bits = test::prbs_bits(48 * symbols);
    EXPECT_EQ(interleaver::deinterleave(interleaver::interleave(bits)), bits)
        << symbols << " symbols";
  }
}

// 802.11a-1999 section 17.3.5.6 with N_CBPS = 48 and N_BPSC = 1, where the
// second permutation does nothing.
TEST(InterleaverTest, MatchesTheStandardBpskPermutation) {
  std::vector<std::uint8_t> in(48);
  std::iota(in.begin(), in.end(), 0);
  std::vector<std::uint8_t> out = interleaver::interleave(in);
  for (std::size_t k = 0; k < 48; k++) {
    std::size_t i = (48 / 16) * (k % 16) + k / 16;
    EXPECT_EQ(out[i], k) << "bit " << k;
  }
}

TEST(InterleaverTest, KeepsEachSymbolSeparate) {
  std::vector<std::uint8_t> in(48 * 3);
  std::iota(in.begin(), in.end(), 0);
  std::vector<std::uint8_t> out = interleaver::interleave(in);
  for (int s = 0; s < 3; s++) {
    std::vector<std::uint8_t> symbol(out.begin() + 48 * s,
                                     out.begin() + 48 * (s + 1));
    std::ranges::sort(symbol);
    EXPECT_EQ(symbol.front(), 48 * s);
    EXPECT_EQ(symbol.back(), 48 * s + 47);
  }
}

class BitInterleaveTest : public testing::TestWithParam<std::size_t> {};

TEST_P(BitInterleaveTest, IsAPermutationWithMatchingInverse) {
  std::size_t bpsc = GetParam();
  std::vector<std::size_t> forward, inverse;
  BitInterleave(48, bpsc).fill(forward, false);
  BitInterleave(48, bpsc).fill(inverse, true);
  ASSERT_EQ(forward.size(), 48 * bpsc);
  std::vector<std::size_t> sorted = forward;
  std::ranges::sort(sorted);
  for (std::size_t k = 0; k < sorted.size(); k++) EXPECT_EQ(sorted[k], k);
  for (std::size_t k = 0; k < forward.size(); k++)
    EXPECT_EQ(inverse[forward[k]], k);
}

// The first permutation puts adjacent coded bits 3 subcarriers apart, and the
// second one only moves bits within a subcarrier.
TEST_P(BitInterleaveTest, PutsAdjacentBitsThreeSubcarriersApart) {
  std::size_t bpsc = GetParam();
  std::vector<std::size_t> forward;
  BitInterleave(48, bpsc).fill(forward, false);
  for (std::size_t k = 0; k + 1 < forward.size(); k++) {
    if (k % 16 == 15) continue;
    EXPECT_EQ(forward[k + 1] / bpsc, forward[k] / bpsc + 3) << "bit " << k;
  }
}

INSTANTIATE_TEST_SUITE_P(Modulations, BitInterleaveTest,
                         testing::Values(1, 2, 4, 6),
                         [](const testing::TestParamInfo<std::size_t>& info) {
                           return std::to_string(info.param) +
                                  "BitsPerSubcarrier";
                         });
}  // namespace
}  // namespace fun
