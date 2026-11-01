#include "frame_builder.h"

#include <gtest/gtest.h>

#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "fft.h"
#include "ppdu.h"
#include "preamble.h"
#include "symbol_mapper.h"
#include "test_util.h"

namespace fun {
namespace {
class FrameBuilderTest : public testing::TestWithParam<Rate> {};

// The preamble (320 samples) and 80 samples (16 cyclic prefix + 64) for the
// SIGNAL symbol and each data symbol.
std::size_t frame_length(Rate rate, std::size_t length) {
  return 320 + 80 * (1 + ppdu(rate, length).get_num_symbols());
}

TEST_P(FrameBuilderTest, HasThePreambleAndWholeSymbols) {
  frame_builder builder;
  for (std::size_t length : {1, 100, 1500}) {
    std::vector<std::complex<double>> frame =
        builder.build_frame(test::prbs_bytes(length), GetParam());
    ASSERT_EQ(frame.size(), frame_length(GetParam(), length))
        << length << " bytes";
    EXPECT_TRUE(
        std::equal(frame.begin(), frame.begin() + 320, PREAMBLE_SAMPLES));
  }
}

TEST_P(FrameBuilderTest, StartsEachSymbolWithACyclicPrefix) {
  std::vector<std::complex<double>> frame =
      frame_builder().build_frame(test::prbs_bytes(100), GetParam());
  for (std::size_t start = 320; start < frame.size(); start += 80) {
    for (std::size_t x = 0; x < 16; x++) {
      ASSERT_EQ(frame[start + x], frame[start + 64 + x])
          << "symbol at " << start << ", sample " << x;
    }
  }
}

// Undoes each step of building the frame, without any of the receiver's
// synchronization or channel estimation.
TEST_P(FrameBuilderTest, DecodesWithAnIdealReceiver) {
  std::vector<std::uint8_t> payload = test::prbs_bytes(500);
  std::vector<std::complex<double>> frame =
      frame_builder().build_frame(payload, GetParam());

  fft f(64);
  std::vector<std::complex<double>> subcarriers;
  for (std::size_t start = 320; start < frame.size(); start += 80) {
    std::complex<double> symbol[64];
    std::copy(frame.begin() + start + 16, frame.begin() + start + 80, symbol);
    f.forward(symbol);
    subcarriers.insert(subcarriers.end(), symbol, symbol + 64);
  }
  std::vector<std::complex<double>> data = symbol_mapper().demap(subcarriers);

  ppdu received;
  ASSERT_TRUE(received.decode_header(
      std::vector<std::complex<double>>(data.begin(), data.begin() + 48)));
  ASSERT_TRUE(received.decode_data(
      std::vector<std::complex<double>>(data.begin() + 48, data.end())));
  EXPECT_EQ(received.get_payload(), payload);
}

INSTANTIATE_TEST_SUITE_P(AllRates, FrameBuilderTest,
                         testing::ValuesIn(test::ALL_RATES), test::rate_name);
}  // namespace
}  // namespace fun
