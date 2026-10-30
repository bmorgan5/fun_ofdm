#include "ppdu.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

#include "test_util.h"

namespace fun {
namespace {
class PpduTest : public testing::TestWithParam<Rate> {};

// 802.11a-1999 section 17.3.5.3: SERVICE (16 bits), the PSDU, and 6 tail bits,
// padded to whole symbols. The PSDU here is the payload and a 4 byte CRC.
int symbol_count(Rate rate, int length) {
  int bits = 16 + 8 * (length + 4) + 6;
  int dbps = RateParams(rate).dbps;
  return (bits + dbps - 1) / dbps;
}

TEST_P(PpduTest, EncodesOneSignalSymbolAndTheDataSymbols) {
  for (int length : {1, 100, 1500}) {
    ppdu frame(test::prbs_bytes(length), GetParam());
    EXPECT_EQ(frame.get_num_symbols(), symbol_count(GetParam(), length));
    EXPECT_EQ(frame.encode().size(),
              48 * (1 + symbol_count(GetParam(), length)))
        << length << " bytes";
  }
}

TEST_P(PpduTest, DecodesWhatItEncodes) {
  for (int length : {1, 100, 1500, 4095}) {
    std::vector<std::uint8_t> payload = test::prbs_bytes(length, length);
    std::vector<std::complex<double>> samples =
        ppdu(payload, GetParam()).encode();

    ppdu received;
    ASSERT_TRUE(received.decode_header(std::vector<std::complex<double>>(
        samples.begin(), samples.begin() + 48)));
    EXPECT_EQ(received.get_rate(), GetParam());
    EXPECT_EQ(received.get_length(), length);
    EXPECT_EQ(received.get_num_symbols(), symbol_count(GetParam(), length));
    ASSERT_TRUE(received.decode_data(std::vector<std::complex<double>>(
        samples.begin() + 48, samples.end())));
    EXPECT_EQ(received.get_payload(), payload) << length << " bytes";
  }
}

TEST_P(PpduTest, RejectsDataWithTooManyErrors) {
  std::vector<std::complex<double>> samples =
      ppdu(test::prbs_bytes(100), GetParam()).encode();
  ppdu received;
  ASSERT_TRUE(received.decode_header(std::vector<std::complex<double>>(
      samples.begin(), samples.begin() + 48)));
  // Rotate every other data point by 180 degrees.
  for (int x = 48; x < samples.size(); x += 2) samples[x] = -samples[x];
  EXPECT_FALSE(received.decode_data(
      std::vector<std::complex<double>>(samples.begin() + 48, samples.end())));
}

INSTANTIATE_TEST_SUITE_P(AllRates, PpduTest, testing::ValuesIn(test::ALL_RATES),
                         test::rate_name);

TEST(PpduSignalTest, CorrectsAnError) {
  std::vector<std::complex<double>> samples =
      ppdu(test::prbs_bytes(100), RATE_3_4_QAM16).encode();
  std::vector<std::complex<double>> header(samples.begin(),
                                           samples.begin() + 48);
  header[20] = -header[20];
  ppdu received;
  ASSERT_TRUE(received.decode_header(header));
  EXPECT_EQ(received.get_rate(), RATE_3_4_QAM16);
  EXPECT_EQ(received.get_length(), 100);
}

// A SIGNAL field decoded from noise should fail the parity or rate check most
// of the time.
TEST(PpduSignalTest, RejectsMostNoise) {
  int accepted = 0;
  for (int trial = 0; trial < 1000; trial++) {
    std::vector<std::uint8_t> bits = test::prbs_bits(48, 48 * trial);
    std::vector<std::complex<double>> header(48);
    for (int x = 0; x < 48; x++) header[x] = bits[x] ? 1 : -1;
    ppdu received;
    if (received.decode_header(header)) accepted++;
  }
  // Parity passes half the time and 11 of the 16 rate fields are valid.
  EXPECT_LT(accepted, 1000 * 0.5 * 11 / 16 * 1.2);
}
}  // namespace
}  // namespace fun
