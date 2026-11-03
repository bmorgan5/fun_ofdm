#include "channel_est.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "ppdu.h"
#include "preamble.h"
#include "symbol_mapper.h"
#include "tagged_vector.h"
#include "test_util.h"

namespace fun {
namespace {
// A channel that scales and rotates each subcarrier differently.
std::complex<double> channel(std::size_t k, double phase) {
  return std::polar(0.5 + 0.01 * static_cast<double>(k),
                    phase * static_cast<double>(k));
}

// The SIGNAL and data symbols of a 100-byte frame, as sent.
std::vector<tagged_vector<64>> sent_symbols(std::size_t offset) {
  std::vector<std::complex<double>> subcarriers = symbol_mapper().map(
      ppdu(test::prbs_bytes(100, offset), RATE_1_2_QAM16).encode());
  std::vector<tagged_vector<64>> symbols;
  for (std::size_t s = 0; s < subcarriers.size(); s += 64) {
    symbols.push_back(tagged_vector<64>(
        {subcarriers.begin() + s, subcarriers.begin() + s + 64}));
  }
  return symbols;
}

// Two LTS symbols, the first tagged LTS_START, and then the symbols, all
// through the channel.
void receive(std::vector<tagged_vector<64>>& received,
             const std::vector<tagged_vector<64>>& symbols, double phase) {
  for (int lts = 0; lts < 2; lts++) {
    tagged_vector<64> symbol(lts == 0 ? LTS_START : NONE);
    for (std::size_t k = 0; k < 64; k++) {
      symbol.samples[k] = LTS_FREQ_DOMAIN[k] * channel(k, phase);
    }
    received.push_back(symbol);
  }
  for (const tagged_vector<64>& sent : symbols) {
    tagged_vector<64> symbol;
    for (std::size_t k = 0; k < 64; k++) {
      symbol.samples[k] = sent.samples[k] * channel(k, phase);
    }
    received.push_back(symbol);
  }
}

// Only the 52 subcarriers the LTS uses can be measured. The null ones come
// out as NaN (0 / 0), and phase_tracker drops them.
void expect_equalized(const std::vector<tagged_vector<64>>& output,
                      std::size_t first,
                      const std::vector<tagged_vector<64>>& sent) {
  ASSERT_GE(output.size(), first + sent.size());
  for (std::size_t s = 0; s < sent.size(); s++) {
    EXPECT_EQ(output[first + s].tag, s == 0 ? START_OF_FRAME : NONE)
        << "symbol " << s;
    for (std::size_t k = 0; k < 64; k++) {
      if (LTS_FREQ_DOMAIN[k] == 0.0) continue;
      ASSERT_NEAR(std::abs(output[first + s].samples[k] - sent[s].samples[k]),
                  0, 1e-9)
          << "symbol " << s << ", subcarrier " << k;
    }
  }
}

TEST(ChannelEstTest, UndoesTheChannel) {
  std::vector<tagged_vector<64>> sent = sent_symbols(0);
  std::vector<tagged_vector<64>> input;
  receive(input, sent, 0.1);
  channel_est block;
  std::vector<tagged_vector<64>> output =
      test::run_block(block, input, input.size());
  EXPECT_EQ(output.size(), sent.size()) << "the LTS symbols are dropped";
  expect_equalized(output, 0, sent);
}

// Until the first LTS, the estimate is 1 on every subcarrier.
TEST(ChannelEstTest, PassesSymbolsBeforeTheFirstFrameThrough) {
  std::vector<tagged_vector<64>> sent = sent_symbols(0);
  channel_est block;
  std::vector<tagged_vector<64>> output =
      test::run_block(block, sent, sent.size());
  ASSERT_EQ(output.size(), sent.size());
  for (std::size_t s = 0; s < sent.size(); s++) {
    EXPECT_EQ(output[s].tag, NONE);
    EXPECT_TRUE(
        std::equal(output[s].samples, output[s].samples + 64, sent[s].samples))
        << "symbol " << s;
  }
}

TEST(ChannelEstTest, MeasuresTheChannelAgainForEachFrame) {
  std::vector<tagged_vector<64>> first = sent_symbols(0);
  std::vector<tagged_vector<64>> second = sent_symbols(800);
  std::vector<tagged_vector<64>> input;
  receive(input, first, 0.1);
  receive(input, second, -0.2);
  channel_est block;
  std::vector<tagged_vector<64>> output =
      test::run_block(block, input, input.size());
  expect_equalized(output, 0, first);
  expect_equalized(output, first.size(), second);
}

TEST(ChannelEstTest, OutputsTheSameInAnyBatchSize) {
  std::vector<tagged_vector<64>> sent = sent_symbols(0);
  std::vector<tagged_vector<64>> input;
  receive(input, sent, 0.1);
  for (std::size_t batch : {1, 2, 3, 7}) {
    channel_est block;
    expect_equalized(test::run_block(block, input, batch), 0, sent);
  }
}
}  // namespace
}  // namespace fun
