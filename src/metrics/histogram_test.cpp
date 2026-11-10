#include "histogram.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "metrics.h"

namespace fun::metrics {
namespace {
// Values below 2048 have buckets of their own, at 3 significant digits, so
// they come back exactly.
histogram one_to_hundred() {
  histogram h;
  for (std::int64_t v = 1; v <= 100; v++) h.record(v);
  return h;
}

class HistogramTest : public testing::Test {
 protected:
  void SetUp() override {
    if (!ENABLED)
      GTEST_SKIP() << "Built without metrics: record() does nothing";
  }
};

TEST_F(HistogramTest, IsEmptyAtFirst) {
  histogram h;
  EXPECT_EQ(h.count(), 0);
  EXPECT_EQ(h.percentile(50), 0);
  EXPECT_EQ(h.max(), 0);
  EXPECT_TRUE(h.buckets().empty());
}

TEST_F(HistogramTest, GivesThePercentiles) {
  histogram h = one_to_hundred();
  EXPECT_EQ(h.count(), 100);
  EXPECT_EQ(h.percentile(50), 50);
  EXPECT_EQ(h.percentile(99), 99);
  EXPECT_EQ(h.percentile(100), 100);
  EXPECT_EQ(h.max(), 100);
}

TEST_F(HistogramTest, KeepsThreeSignificantDigits) {
  histogram h;
  h.record(123'456'789);
  EXPECT_NEAR(h.max(), 123'456'789, 123'456'789 / 1000);
}

TEST_F(HistogramTest, ClampsValuesOutsideItsRange) {
  histogram h;
  h.record(-5);
  h.record(histogram::MAX_VALUE * 2);
  EXPECT_EQ(h.count(), 2);
  EXPECT_EQ(h.percentile(0), 0);
  EXPECT_NEAR(h.max(), histogram::MAX_VALUE, histogram::MAX_VALUE / 1000);
}

TEST_F(HistogramTest, ListsTheBucketsThatHoldValues) {
  histogram h;
  h.record(7);
  h.record(7);
  h.record(1000);
  std::vector<histogram::bucket> buckets = h.buckets();
  ASSERT_EQ(buckets.size(), 2u);
  EXPECT_EQ(buckets[0].value, 7);
  EXPECT_EQ(buckets[0].count, 2);
  EXPECT_EQ(buckets[1].value, 1000);
  EXPECT_EQ(buckets[1].count, 1);
}

TEST_F(HistogramTest, AddsAnotherHistogram) {
  histogram h = one_to_hundred();
  histogram more;
  more.record(1000);
  h.add(more);
  EXPECT_EQ(h.count(), 101);
  EXPECT_EQ(h.max(), 1000);
}

TEST_F(HistogramTest, CopiesItsValues) {
  histogram h = one_to_hundred();
  histogram copy(h);
  histogram assigned;
  assigned.record(5000);
  assigned = h;
  h.record(1000);
  EXPECT_EQ(copy.count(), 100);
  EXPECT_EQ(copy.max(), 100);
  EXPECT_EQ(assigned.count(), 100);
  EXPECT_EQ(assigned.max(), 100);
}

TEST(HistogramDisabledTest, RecordsNothingWithoutMetrics) {
  if (ENABLED) GTEST_SKIP() << "Built with metrics";
  histogram h;
  h.record(100);
  EXPECT_EQ(h.count(), 0);
}
}  // namespace
}  // namespace fun::metrics
