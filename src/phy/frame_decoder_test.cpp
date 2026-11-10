#include "frame_decoder.h"

#include <gtest/gtest.h>

#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "ppdu.h"
#include "tagged_vector.h"
#include "test_util.h"

namespace fun {
namespace {
class FrameDecoderTest : public testing::TestWithParam<Rate> {};

// The SIGNAL and data symbols of a frame, as phase_tracker passes them on:
// 48 data subcarriers each, the first tagged START_OF_FRAME.
std::vector<tagged_vector<48>> symbols(const std::vector<std::uint8_t>& payload,
                                       Rate rate) {
  std::vector<std::complex<double>> samples = ppdu(payload, rate).encode();
  std::vector<tagged_vector<48>> symbols;
  for (std::size_t s = 0; s < samples.size(); s += 48) {
    symbols.push_back(
        tagged_vector<48>({samples.begin() + s, samples.begin() + s + 48},
                          s == 0 ? START_OF_FRAME : NONE));
  }
  return symbols;
}

TEST_P(FrameDecoderTest, DecodesTheFrame) {
  for (std::size_t length : {1, 100, 1500, 4095}) {
    std::vector<std::uint8_t> payload = test::prbs_bytes(length, length);
    std::vector<tagged_vector<48>> input = symbols(payload, GetParam());
    frame_decoder block;
    EXPECT_EQ(test::run_block(block, input, input.size()),
              std::vector<std::vector<std::uint8_t>>{payload})
        << length << " bytes";
  }
}

TEST_P(FrameDecoderTest, DecodesAFrameSplitAcrossBatches) {
  std::vector<std::uint8_t> payload = test::prbs_bytes(100);
  std::vector<tagged_vector<48>> input = symbols(payload, GetParam());
  for (std::size_t batch : {1, 2, 7}) {
    frame_decoder block;
    EXPECT_EQ(test::run_block(block, input, batch),
              std::vector<std::vector<std::uint8_t>>{payload})
        << batch << " symbols per batch";
  }
}

TEST_P(FrameDecoderTest, DecodesFramesBackToBack) {
  std::vector<std::uint8_t> first = test::prbs_bytes(100);
  std::vector<std::uint8_t> second = test::prbs_bytes(200, 800);
  std::vector<tagged_vector<48>> input = symbols(first, GetParam());
  std::vector<tagged_vector<48>> more = symbols(second, GetParam());
  input.insert(input.end(), more.begin(), more.end());
  frame_decoder block;
  EXPECT_EQ(test::run_block(block, input, input.size()),
            (std::vector<std::vector<std::uint8_t>>{first, second}));
}

// Symbols before a START_OF_FRAME, like the ones fft_symbols cuts from the
// noise between frames, are ignored.
TEST_P(FrameDecoderTest, IgnoresSymbolsOutsideAFrame) {
  std::vector<std::uint8_t> payload = test::prbs_bytes(100);
  std::vector<tagged_vector<48>> input(
      10, tagged_vector<48>(test::prbs_noise(48, 1)));
  std::vector<tagged_vector<48>> frame = symbols(payload, GetParam());
  input.insert(input.end(), frame.begin(), frame.end());
  frame_decoder block;
  EXPECT_EQ(test::run_block(block, input, input.size()),
            std::vector<std::vector<std::uint8_t>>{payload});
}

// Negating every point of a data symbol is more than the Viterbi decoder can
// correct, so the CRC fails.
TEST_P(FrameDecoderTest, DropsAFrameWithABadCrc) {
  std::vector<tagged_vector<48>> input =
      symbols(test::prbs_bytes(100), GetParam());
  for (std::complex<double>& point : input[1].samples) point = -point;
  frame_decoder block;
  EXPECT_TRUE(test::run_block(block, input, input.size()).empty());
}

// Each symbol ends 80 samples after the one before it.
TEST_P(FrameDecoderTest, CountsAFrameThatPassesItsCrc) {
  std::vector<tagged_vector<48>> input =
      symbols(test::prbs_bytes(100), GetParam());
  for (std::size_t s = 0; s < input.size(); s++)
    input[s].last_sample = 1000 + 80 * s;
  frame_decoder block;
  test::run_block(block, input, input.size());
  EXPECT_EQ(block.frame_ends,
            std::vector<std::uint64_t>{1000 + 80 * (input.size() - 1)});
  for (int rate = 0; rate < static_cast<int>(RATE_COUNT); rate++) {
    EXPECT_EQ(block.crc_ok()[rate], rate == GetParam() ? 1u : 0u) << rate;
    EXPECT_EQ(block.crc_failed()[rate], 0u) << rate;
  }
  EXPECT_EQ(block.signal_failed(), 0u);
}

TEST_P(FrameDecoderTest, CountsAFrameThatFailsItsCrc) {
  std::vector<tagged_vector<48>> input =
      symbols(test::prbs_bytes(100), GetParam());
  for (std::complex<double>& point : input[1].samples) point = -point;
  frame_decoder block;
  test::run_block(block, input, input.size());
  EXPECT_TRUE(block.frame_ends.empty());
  EXPECT_EQ(block.crc_ok()[GetParam()], 0u);
  EXPECT_EQ(block.crc_failed()[GetParam()], 1u);
}

// Negating the SIGNAL symbol flips every bit, which the parity bit catches.
TEST_P(FrameDecoderTest, CountsASignalThatFailsItsParityCheck) {
  std::vector<tagged_vector<48>> input =
      symbols(test::prbs_bytes(100), GetParam());
  for (std::complex<double>& point : input[0].samples) point = -point;
  frame_decoder block;
  EXPECT_TRUE(test::run_block(block, input, input.size()).empty());
  EXPECT_EQ(block.signal_failed(), 1u);
  EXPECT_EQ(block.crc_ok()[GetParam()] + block.crc_failed()[GetParam()], 0u);
}

INSTANTIATE_TEST_SUITE_P(AllRates, FrameDecoderTest,
                         testing::ValuesIn(test::ALL_RATES), test::rate_name);
}  // namespace
}  // namespace fun
