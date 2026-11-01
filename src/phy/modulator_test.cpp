#include "modulator.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "test_util.h"

namespace fun {
namespace {
class ModulatorTest : public testing::TestWithParam<Rate> {};

// Every bit pattern for one subcarrier, in order: 0...0, 0...1, and so on.
std::vector<std::uint8_t> every_pattern(int bpsc) {
  std::vector<std::uint8_t> bits;
  for (int p = 0; p < (1 << bpsc); p++) {
    for (int b = bpsc - 1; b >= 0; b--) bits.push_back((p >> b) & 1);
  }
  return bits;
}

TEST_P(ModulatorTest, MakesOnePointPerSubcarrier) {
  RateParams rp(GetParam());
  EXPECT_EQ(modulator::modulate(test::prbs_bits(rp.cbps * 3), rp.rate).size(),
            48u * 3);
}

TEST_P(ModulatorTest, HasUnitAveragePower) {
  RateParams rp(GetParam());
  std::vector<std::complex<double>> points =
      modulator::modulate(every_pattern(rp.bpsc), rp.rate);
  ASSERT_EQ(points.size(), std::size_t{1} << rp.bpsc);
  double power = 0;
  for (std::complex<double> p : points) power += std::norm(p);
  EXPECT_NEAR(power / points.size(), 1.0, 1e-12);
}

TEST_P(ModulatorTest, DemodulatesToConfidentBits) {
  RateParams rp(GetParam());
  std::vector<std::uint8_t> bits = test::prbs_bits(rp.cbps * 10);
  std::vector<std::uint8_t> soft =
      modulator::demodulate(modulator::modulate(bits, rp.rate), rp.rate);
  ASSERT_EQ(soft.size(), bits.size());
  for (std::size_t x = 0; x < bits.size(); x++) {
    // 0 is a confident 0 and 255 is a confident 1. The bits that pick an inner
    // level of QAM16 or QAM64 are only one level from the boundary, so they
    // are the least confident, at 32 from the middle (128).
    if (bits[x])
      EXPECT_GE(soft[x], 160) << "bit " << x;
    else
      EXPECT_LE(soft[x], 96) << "bit " << x;
  }
}

TEST_P(ModulatorTest, DemodulatesWithNoise) {
  RateParams rp(GetParam());
  std::vector<std::uint8_t> bits = test::prbs_bits(rp.cbps * 10);
  std::vector<std::complex<double>> points = modulator::modulate(bits, rp.rate);
  // Move each point by less than half the distance to its neighbours, which is
  // 1/sqrt(42) for QAM64 (the closest).
  for (std::size_t x = 0; x < points.size(); x++) {
    points[x] += std::polar(0.9 / std::sqrt(42.0), x * 0.7);
  }
  std::vector<std::uint8_t> soft = modulator::demodulate(points, rp.rate);
  for (std::size_t x = 0; x < bits.size(); x++) {
    EXPECT_EQ(soft[x] > 127, bits[x] == 1) << "bit " << x;
  }
}

INSTANTIATE_TEST_SUITE_P(AllRates, ModulatorTest,
                         testing::ValuesIn(test::ALL_RATES), test::rate_name);

// 802.11a-1999 section 17.3.5.7, tables 81 to 84. The first half of the bits
// for a subcarrier pick I and the second half pick Q, Gray coded, and K_MOD
// normalizes the power.
void expect_standard_mapping(Rate rate, int bpsc,
                             const std::vector<int>& levels, double k_mod) {
  std::vector<std::complex<double>> points =
      modulator::modulate(every_pattern(bpsc), rate);
  int axis_bits = std::max(bpsc / 2, 1);
  for (std::size_t p = 0; p < points.size(); p++) {
    double i = levels[p >> (bpsc - axis_bits)] * k_mod;
    double q = bpsc == 1 ? 0 : levels[p & ((1 << axis_bits) - 1)] * k_mod;
    EXPECT_NEAR(points[p].real(), i, 1e-12) << "pattern " << p;
    EXPECT_NEAR(points[p].imag(), q, 1e-12) << "pattern " << p;
  }
}

TEST(ModulatorStandardTest, Bpsk) {
  expect_standard_mapping(RATE_1_2_BPSK, 1, {-1, 1}, 1);
}

TEST(ModulatorStandardTest, Qpsk) {
  expect_standard_mapping(RATE_1_2_QPSK, 2, {-1, 1}, 1 / std::sqrt(2.0));
}

TEST(ModulatorStandardTest, Qam16) {
  // Indexed by b0 b1 (or b2 b3): 00, 01, 10, 11.
  expect_standard_mapping(RATE_1_2_QAM16, 4, {-3, -1, 3, 1},
                          1 / std::sqrt(10.0));
}

TEST(ModulatorStandardTest, Qam64) {
  // Indexed by b0 b1 b2 (or b3 b4 b5): 000, 001, 010, ..., 111.
  expect_standard_mapping(RATE_2_3_QAM64, 6, {-7, -5, -1, -3, 7, 5, 1, 3},
                          1 / std::sqrt(42.0));
}
}  // namespace
}  // namespace fun
