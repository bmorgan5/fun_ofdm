#include "circular_accumulator.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <numeric>
#include <vector>

#include "test_util.h"

namespace fun {
namespace {
TEST(CircularAccumulatorTest, StartsAtZero) {
  circular_accumulator<int> acc(4);
  EXPECT_EQ(acc.sum, 0);
}

TEST(CircularAccumulatorTest, SumsOnlyTheMostRecentSamples) {
  const int size = 16;
  circular_accumulator<int> acc(size);
  std::vector<std::uint8_t> data = test::prbs_bytes(10 * size);
  std::deque<int> window;
  for (std::size_t x = 0; x < data.size(); x++) {
    int sample = int(data[x]) - 128;
    acc.add(sample);
    window.push_back(sample);
    if (window.size() > size) window.pop_front();
    ASSERT_EQ(acc.sum, std::accumulate(window.begin(), window.end(), 0))
        << "after sample " << x;
  }
}

TEST(CircularAccumulatorTest, TreatsNanAsZero) {
  circular_accumulator<double> acc(2);
  acc.add(1.0);
  acc.add(NAN);
  EXPECT_EQ(acc.sum, 1.0);
  acc.add(2.0);
  EXPECT_EQ(acc.sum, 2.0);
}
}  // namespace
}  // namespace fun
