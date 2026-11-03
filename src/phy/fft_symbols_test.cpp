#include "fft_symbols.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "frame_builder.h"
#include "ppdu.h"
#include "preamble.h"
#include "symbol_mapper.h"
#include "tagged_vector.h"
#include "test_util.h"

namespace fun {
namespace {
// Where the frame starts in the input.
constexpr std::size_t START = 500;

// The data: 100 bytes at 1/2 BPSK.
const std::vector<std::uint8_t> PAYLOAD = test::prbs_bytes(100);
constexpr Rate RATE = RATE_1_2_BPSK;

// A frame at START, between zeros, with LTS1 and LTS2 at the LTS symbols,
// early samples before each.
std::vector<tagged_sample> tagged_frame(std::size_t early = 0) {
  std::vector<std::complex<double>> frame =
      frame_builder().build_frame(PAYLOAD, RATE);
  std::vector<tagged_sample> samples(START + frame.size() + 500);
  for (tagged_sample& s : samples) s.sample = 0;
  for (std::size_t x = 0; x < frame.size(); x++) {
    samples[START + x].sample = frame[x];
  }
  samples[START + 192 - early].tag = LTS1;
  samples[START + 256 - early].tag = LTS2;
  return samples;
}

// The subcarriers the frame was built from: the two LTS symbols, then the
// SIGNAL and data symbols.
std::vector<std::complex<double>> sent_subcarriers() {
  std::vector<std::complex<double>> subcarriers(LTS_FREQ_DOMAIN,
                                                LTS_FREQ_DOMAIN + 64);
  subcarriers.insert(subcarriers.end(), LTS_FREQ_DOMAIN, LTS_FREQ_DOMAIN + 64);
  std::vector<std::complex<double>> symbols =
      symbol_mapper().map(ppdu(PAYLOAD, RATE).encode());
  subcarriers.insert(subcarriers.end(), symbols.begin(), symbols.end());
  return subcarriers;
}

// Returns the output from the symbol tagged LTS_START on, and checks there's
// only one.
std::vector<tagged_vector<64>> from_lts_start(
    const std::vector<tagged_vector<64>>& output) {
  std::size_t start = output.size();
  for (std::size_t x = 0; x < output.size(); x++) {
    if (output[x].tag == LTS_START) {
      EXPECT_EQ(start, output.size()) << "a second LTS_START at " << x;
      start = x;
    }
  }
  EXPECT_LT(start, output.size()) << "no LTS_START";
  return {output.begin() + std::min(start, output.size()), output.end()};
}

TEST(FftSymbolsTest, RecoversTheSentSubcarriers) {
  std::vector<tagged_sample> input = tagged_frame();
  fft_symbols block;
  std::vector<tagged_vector<64>> output =
      from_lts_start(test::run_block(block, input, input.size()));
  std::vector<std::complex<double>> sent = sent_subcarriers();
  ASSERT_GE(output.size(), sent.size() / 64);
  for (std::size_t s = 0; s < sent.size() / 64; s++) {
    EXPECT_EQ(output[s].tag, s == 0 ? LTS_START : NONE) << "symbol " << s;
    for (std::size_t k = 0; k < 64; k++) {
      ASSERT_NEAR(std::abs(output[s].samples[k] - sent[64 * s + k]), 0, 1e-9)
          << "symbol " << s << ", subcarrier " << k;
    }
  }
}

// timing_sync tags the symbols 8 samples early. The window then holds the
// last 8 samples of the cyclic prefix and the first 56 of the symbol, a
// circular shift, which turns into a phase of -2π·8k/64 on subcarrier k
// (numbered from -32). channel_est measures and removes it with the channel.
TEST(FftSymbolsTest, AnEarlyWindowRotatesEachSubcarrier) {
  std::vector<tagged_sample> input = tagged_frame(8);
  fft_symbols block;
  std::vector<tagged_vector<64>> output =
      from_lts_start(test::run_block(block, input, input.size()));
  std::vector<std::complex<double>> sent = sent_subcarriers();
  ASSERT_GE(output.size(), sent.size() / 64);
  for (std::size_t s = 0; s < sent.size() / 64; s++) {
    for (std::size_t k = 0; k < 64; k++) {
      double subcarrier = static_cast<double>(k) - 32;
      std::complex<double> expected =
          sent[64 * s + k] * std::polar(1.0, -2 * M_PI * 8 * subcarrier / 64);
      ASSERT_NEAR(std::abs(output[s].samples[k] - expected), 0, 1e-9)
          << "symbol " << s << ", subcarrier " << k;
    }
  }
}

TEST(FftSymbolsTest, OutputsTheSameInAnyBatchSize) {
  std::vector<tagged_sample> input = tagged_frame(8);
  fft_symbols whole;
  std::vector<tagged_vector<64>> expected =
      test::run_block(whole, input, input.size());
  for (std::size_t batch : {1, 7, 64, 80, 100, 4096}) {
    fft_symbols block;
    std::vector<tagged_vector<64>> output =
        test::run_block(block, input, batch);
    ASSERT_EQ(output.size(), expected.size()) << batch << " samples per batch";
    for (std::size_t s = 0; s < output.size(); s++) {
      ASSERT_EQ(output[s].tag, expected[s].tag)
          << batch << " samples per batch, symbol " << s;
      ASSERT_TRUE(std::equal(output[s].samples, output[s].samples + 64,
                             expected[s].samples))
          << batch << " samples per batch, symbol " << s;
    }
  }
}
}  // namespace
}  // namespace fun
