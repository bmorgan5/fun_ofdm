#include "phase_tracker.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

#include "symbol_mapper.h"
#include "tagged_vector.h"
#include "test_util.h"

namespace fun {
namespace {
// 130 symbols, so the 127-symbol pilot polarity sequence wraps around.
constexpr std::size_t SYMBOLS = 130;

// The data subcarriers: QPSK points from PRBS-15.
std::vector<std::complex<double>> data() {
  return test::prbs_noise(48 * SYMBOLS, 1 / std::sqrt(2.0));
}

// The rotation of symbol s: different for each symbol, from -1.5 to about
// 1.5 radians, inside ±π so that it can be measured.
double rotation(std::size_t s) { return -1.5 + 0.023 * static_cast<double>(s); }

// The data mapped onto subcarriers with the pilots, each symbol rotated, and
// the first tagged START_OF_FRAME.
std::vector<tagged_vector<64>> rotated_symbols() {
  std::vector<std::complex<double>> subcarriers = symbol_mapper().map(data());
  std::vector<tagged_vector<64>> symbols;
  for (std::size_t s = 0; s < SYMBOLS; s++) {
    tagged_vector<64> symbol(s == 0 ? START_OF_FRAME : NONE);
    for (std::size_t k = 0; k < 64; k++) {
      symbol.samples[k] =
          subcarriers[64 * s + k] * std::polar(1.0, rotation(s));
    }
    symbols.push_back(symbol);
  }
  return symbols;
}

void expect_data(const std::vector<tagged_vector<48>>& output) {
  std::vector<std::complex<double>> expected = data();
  ASSERT_EQ(output.size(), SYMBOLS);
  for (std::size_t s = 0; s < SYMBOLS; s++) {
    EXPECT_EQ(output[s].tag, s == 0 ? START_OF_FRAME : NONE) << "symbol " << s;
    for (std::size_t d = 0; d < 48; d++) {
      ASSERT_NEAR(std::abs(output[s].samples[d] - expected[48 * s + d]), 0,
                  1e-9)
          << "symbol " << s << ", data subcarrier " << d;
    }
  }
}

TEST(PhaseTrackerTest, UndoesTheRotationOfEachSymbol) {
  std::vector<tagged_vector<64>> input = rotated_symbols();
  phase_tracker block;
  expect_data(test::run_block(block, input, input.size()));
}

TEST(PhaseTrackerTest, OutputsTheSameInAnyBatchSize) {
  std::vector<tagged_vector<64>> input = rotated_symbols();
  for (std::size_t batch : {1, 7, 64}) {
    phase_tracker block;
    expect_data(test::run_block(block, input, batch));
  }
}

// START_OF_FRAME starts the pilot polarity sequence again.
TEST(PhaseTrackerTest, RestartsThePilotPolarityForEachFrame) {
  std::vector<tagged_vector<64>> input = rotated_symbols();
  phase_tracker block;
  test::run_block(block, {input.begin(), input.begin() + 50}, 50);
  expect_data(test::run_block(block, input, input.size()));
}
}  // namespace
}  // namespace fun
