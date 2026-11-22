#include "snr.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <vector>

#include "preamble.h"
#include "test_util.h"

namespace fun {
namespace {
// A channel that scales and rotates each subcarrier differently.
std::complex<double> channel(std::size_t k) {
  return std::polar(0.5 + 0.01 * static_cast<double>(k),
                    0.1 * static_cast<double>(k));
}

// The LTS through the channel: the signal on each subcarrier.
std::vector<std::complex<double>> received_lts() {
  std::vector<std::complex<double>> lts(64);
  for (std::size_t k = 0; k < 64; k++) lts[k] = LTS_FREQ_DOMAIN[k] * channel(k);
  return lts;
}

// The LTS's power over all 64 subcarriers.
double power(const std::vector<std::complex<double>>& symbol) {
  double sum = 0;
  for (const std::complex<double>& x : symbol) sum += std::norm(x);
  return sum;
}

// The estimates of `frames` frames at an SNR in dB, each with its own noise
// from the PRBS, at most 127 before PRBS-15 repeats. The noise has the power
// that gives that SNR in the time domain: signal / (64 * noise per
// subcarrier).
std::vector<double> estimates(double snr_db, std::size_t frames) {
  std::vector<std::complex<double>> lts = received_lts();
  double noise_power = power(lts) / (64 * std::pow(10, snr_db / 10));
  // prbs_noise's points have a power of 2 amplitude²
  double amplitude = std::sqrt(noise_power / 2);
  std::vector<std::complex<double>> noise =
      test::prbs_noise(128 * frames, amplitude);
  std::vector<double> result;
  for (std::size_t f = 0; f < frames; f++) {
    std::vector<std::complex<double>> first(64), second(64);
    for (std::size_t k = 0; k < 64; k++) {
      first[k] = lts[k] + noise[128 * f + k];
      second[k] = lts[k] + noise[128 * f + 64 + k];
    }
    result.push_back(lts_snr_db(first.data(), second.data()));
  }
  return result;
}

double median(std::vector<double> values) {
  std::sort(values.begin(), values.end());
  return values[values.size() / 2];
}

TEST(LtsSnrTest, IsInfiniteWithoutNoise) {
  std::vector<std::complex<double>> lts = received_lts();
  EXPECT_EQ(lts_snr_db(lts.data(), lts.data()),
            std::numeric_limits<double>::infinity());
}

TEST(LtsSnrTest, IsMinusInfinityWhenTheSymbolsCancel) {
  std::vector<std::complex<double>> first = received_lts();
  std::vector<std::complex<double>> second = first;
  for (std::complex<double>& x : second) x = -x;
  EXPECT_EQ(lts_snr_db(first.data(), second.data()),
            -std::numeric_limits<double>::infinity());
}

// Within 1 dB from 0 to 30 dB is the target. The median of many frames is
// much closer. One frame's noise is measured on only 52 subcarriers, so each
// frame is within about ±0.6 dB, and the worst of 100 within 2 dB.
TEST(LtsSnrTest, EstimatesTheSnrFrom0To30Db) {
  for (int snr_db = 0; snr_db <= 30; snr_db++) {
    std::vector<double> found = estimates(snr_db, 100);
    EXPECT_NEAR(median(found), snr_db, 0.25) << snr_db << " dB";
  }
}

TEST(LtsSnrTest, EachFrameIsWithin2DbAt20Db) {
  for (double db : estimates(20, 100)) EXPECT_NEAR(db, 20, 2.0);
}

TEST(LtsSnrTest, IgnoresTheNullSubcarriers) {
  std::vector<std::complex<double>> first = received_lts();
  std::vector<std::complex<double>> second = first;
  // Only the null subcarriers differ, so it's as if there were no noise
  for (std::size_t k = 0; k < 64; k++) {
    if (LTS_FREQ_DOMAIN[k] == 0.0) second[k] = {1, 1};
  }
  EXPECT_EQ(lts_snr_db(first.data(), second.data()),
            std::numeric_limits<double>::infinity());
}

TEST(SnrHistogramTest, HasNoPercentilesWhenEmpty) {
  snr_histogram h;
  EXPECT_EQ(h.count(), 0u);
  EXPECT_TRUE(std::isnan(h.percentile(50)));
}

TEST(SnrHistogramTest, ReturnsTheMiddleOfEachBin) {
  snr_histogram h;
  for (double db : {10.02, 12.04, 14.06, 16.08}) h.record(db);
  EXPECT_EQ(h.count(), 4u);
  EXPECT_NEAR(h.percentile(25), 10.05, 1e-9);
  EXPECT_NEAR(h.percentile(50), 12.05, 1e-9);
  EXPECT_NEAR(h.percentile(75), 14.05, 1e-9);
  EXPECT_NEAR(h.percentile(100), 16.05, 1e-9);
}

TEST(SnrHistogramTest, RanksPercentilesThatArentExact) {
  snr_histogram h;
  for (int i = 0; i < 100; i++) h.record(static_cast<double>(i) / 10);
  // 7 / 100 * 100 is 7.000000000000001 in doubles: the 7th value, not the 8th
  EXPECT_NEAR(h.percentile(7), 0.65, 1e-9);
}

TEST(SnrHistogramTest, CountsValuesOutsideItsRangeAtItsEnds) {
  snr_histogram h;
  h.record(-std::numeric_limits<double>::infinity());
  h.record(std::numeric_limits<double>::infinity());
  EXPECT_NEAR(h.percentile(0), snr_histogram::MIN_DB + 0.05, 1e-9);
  EXPECT_NEAR(h.percentile(100), snr_histogram::MAX_DB - 0.05, 1e-9);
}

TEST(SnrHistogramTest, DoesntRecordNan) {
  snr_histogram h;
  h.record(std::numeric_limits<double>::quiet_NaN());
  EXPECT_EQ(h.count(), 0u);
}
}  // namespace
}  // namespace fun
