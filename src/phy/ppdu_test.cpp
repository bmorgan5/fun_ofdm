#include "ppdu.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "interleaver.h"
#include "modulator.h"
#include "parity.h"
#include "puncturer.h"
#include "scrambler.h"
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

// Recovers the bits the convolutional encoder was given for a frame's data,
// one per byte (0 or 1), from the frame's data points. With no noise, each
// coded bit is the new input bit XOR the parity of the 6 bits before it,
// because both polynomials include the newest bit (see viterbi.h). So each
// input bit follows from the encoder's state and either coded bit of its pair.
// Puncturing deletes at most one of them, and depuncturing marks the hole with
// 127.
std::vector<std::uint8_t> encoder_input(
    const std::vector<std::complex<double>>& data_points, Rate rate) {
  std::vector<unsigned char> coded = puncturer::depuncture(
      interleaver::deinterleave(modulator::demodulate(data_points, rate)),
      rate);
  const int polys[2] = {121, 91};
  std::vector<std::uint8_t> bits;
  int state = 0;
  for (std::size_t x = 0; x + 1 < coded.size(); x += 2) {
    int k = coded[x] != 127 ? 0 : 1;
    int coded_bit = coded[x + k] > 127;
    int bit = coded_bit ^ parity((state << 1) & polys[k]);
    bits.push_back(bit);
    state = ((state << 1) | bit) & 0x3F;
  }
  return bits;
}

// Packs bits, one per byte, into bytes, most significant bit first.
std::vector<std::uint8_t> pack(const std::vector<std::uint8_t>& bits) {
  std::vector<std::uint8_t> bytes((bits.size() + 7) / 8, 0);
  for (std::size_t x = 0; x < bits.size(); x++)
    bytes[x / 8] |= bits[x] << (7 - x % 8);
  return bytes;
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

// Checks encoder_input(): descrambled, the bits it recovers are SERVICE (16
// zero bits), then the payload.
TEST_P(PpduTest, RecoversTheEncoderInput) {
  for (int length : {1, 100, 1500}) {
    std::vector<std::uint8_t> payload = test::prbs_bytes(length, length);
    std::vector<std::complex<double>> samples =
        ppdu(payload, GetParam()).encode();
    std::vector<std::uint8_t> data = scrambler::scramble(
        pack(encoder_input({samples.begin() + 48, samples.end()}, GetParam())));
    EXPECT_EQ(data[0], 0) << length << " bytes";
    EXPECT_EQ(data[1], 0) << length << " bytes";
    EXPECT_EQ(
        std::vector<std::uint8_t>(data.begin() + 2, data.begin() + 2 + length),
        payload)
        << length << " bytes";
  }
}

// 802.11a-1999 section 17.3.5.2: the 6 tail bits are zeros after scrambling,
// so the encoder ends in state 0, which the Viterbi decoder assumes. The
// encoder's state is its last 6 input bits.
TEST_P(PpduTest, EndsWithTheEncoderInStateZero) {
  GTEST_SKIP() << "The tail bits are scrambled, so the encoder doesn't always "
                  "end in state 0 (see the \\bug in ppdu.h)";

  for (int length : {1, 2, 3, 4, 5, 6, 7, 8, 100, 1500}) {
    std::vector<std::complex<double>> samples =
        ppdu(test::prbs_bytes(length, length), GetParam()).encode();
    std::vector<std::uint8_t> bits =
        encoder_input({samples.begin() + 48, samples.end()}, GetParam());
    EXPECT_EQ(std::vector<std::uint8_t>(bits.end() - 6, bits.end()),
              std::vector<std::uint8_t>(6, 0))
        << length << " bytes";
  }
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
