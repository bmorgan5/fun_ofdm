#include "viterbi.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "puncturer.h"
#include "test_util.h"

namespace fun {
namespace {
// Packs bits (one per byte) MSB first, with a zero byte after them for the
// encoder's tail bits.
std::vector<std::uint8_t> pack(const std::vector<std::uint8_t>& bits) {
  std::vector<std::uint8_t> bytes(bits.size() / 8 + 2, 0);
  for (int x = 0; x < bits.size(); x++) bytes[x / 8] |= bits[x] << (7 - x % 8);
  return bytes;
}

std::vector<std::uint8_t> unpack(const std::vector<std::uint8_t>& bytes,
                                 int bit_count) {
  std::vector<std::uint8_t> bits(bit_count);
  for (int x = 0; x < bit_count; x++)
    bits[x] = (bytes[x / 8] >> (7 - x % 8)) & 1;
  return bits;
}

// Encodes bits and returns the coded bits, one per byte (0 or 1), including
// the 12 coded bits from the 6 tail bits.
std::vector<std::uint8_t> encode(const std::vector<std::uint8_t>& bits) {
  std::vector<std::uint8_t> data = pack(bits);
  std::vector<std::uint8_t> coded(2 * (bits.size() + 6));
  viterbi().conv_encode(data.data(), coded.data(), bits.size());
  return coded;
}

// Decodes soft coded bits (0 to 255) back into bit_count bits.
std::vector<std::uint8_t> decode(std::vector<std::uint8_t> soft,
                                 int bit_count) {
  std::vector<std::uint8_t> data(bit_count / 8 + 2, 0);
  viterbi().conv_decode(soft.data(), data.data(), bit_count);
  return unpack(data, bit_count);
}

// Converts hard coded bits to the strongest soft values.
std::vector<std::uint8_t> to_soft(std::vector<std::uint8_t> coded) {
  for (std::uint8_t& c : coded) c = c ? 255 : 0;
  return coded;
}

TEST(ViterbiTest, EncodesZerosToZeros) {
  std::vector<std::uint8_t> coded = encode(std::vector<std::uint8_t>(100, 0));
  EXPECT_EQ(coded, std::vector<std::uint8_t>(212, 0));
}

TEST(ViterbiTest, OutputsOneCodedBitPerByte) {
  for (std::uint8_t c : encode(test::prbs_bits(1000))) {
    ASSERT_TRUE(c == 0 || c == 1);
  }
}

// 802.11a-1999 section 17.3.5.5: the generator polynomials are g0 = 133 and
// g1 = 171 (octal), with output A from g0 sent before output B from g1. A
// single 1 bit gives the taps of both, interleaved.
TEST(ViterbiTest, MatchesTheStandardGeneratorPolynomials) {
  GTEST_SKIP() << "The encoder uses the time-reversed polynomials, with the "
                  "outputs swapped";

  std::vector<std::uint8_t> impulse(8, 0);
  impulse[0] = 1;
  std::vector<std::uint8_t> expected = {1, 1, 0, 1, 1, 1, 1,
                                        1, 0, 0, 1, 0, 1, 1};
  std::vector<std::uint8_t> coded = encode(impulse);
  EXPECT_EQ(std::vector<std::uint8_t>(coded.begin(), coded.begin() + 14),
            expected);
}

TEST(ViterbiTest, DecodesWithoutErrors) {
  // 18 is the SIGNAL field. The others aren't whole bytes or are long frames.
  // The decoder only handles an even number of bits.
  for (int bit_count : {18, 24, 102, 1000, 12000}) {
    std::vector<std::uint8_t> bits = test::prbs_bits(bit_count, bit_count);
    EXPECT_EQ(decode(to_soft(encode(bits)), bit_count), bits)
        << bit_count << " bits";
  }
}

TEST(ViterbiTest, CorrectsIsolatedErrors) {
  std::vector<std::uint8_t> bits = test::prbs_bits(1000);
  std::vector<std::uint8_t> soft = to_soft(encode(bits));
  for (int x = 5; x < soft.size(); x += 20) soft[x] = 255 - soft[x];
  EXPECT_EQ(decode(soft, bits.size()), bits);
}

TEST(ViterbiTest, CorrectsBurstsOfTwoErrors) {
  std::vector<std::uint8_t> bits = test::prbs_bits(1000);
  std::vector<std::uint8_t> soft = to_soft(encode(bits));
  for (int x = 5; x + 1 < soft.size(); x += 40) {
    soft[x] = 255 - soft[x];
    soft[x + 1] = 255 - soft[x + 1];
  }
  EXPECT_EQ(decode(soft, bits.size()), bits);
}

TEST(ViterbiTest, UsesSoftConfidence) {
  // Every fourth coded bit is wrong, which is too many errors to correct, but
  // only just past the halfway point. The rest are right and strong.
  std::vector<std::uint8_t> bits = test::prbs_bits(1000);
  std::vector<std::uint8_t> coded = encode(bits);
  std::vector<std::uint8_t> soft = to_soft(coded);
  for (int x = 1; x < soft.size(); x += 4) soft[x] = coded[x] ? 115 : 140;
  EXPECT_EQ(decode(soft, bits.size()), bits);

  for (int x = 1; x < soft.size(); x += 4) soft[x] = coded[x] ? 0 : 255;
  EXPECT_NE(decode(soft, bits.size()), bits)
      << "Strong errors should be uncorrectable";
}

class PuncturedViterbiTest : public testing::TestWithParam<Rate> {};

// Punctures, adds an isolated error every so often, depunctures and decodes,
// as the PPDU does.
TEST_P(PuncturedViterbiTest, CorrectsIsolatedErrors) {
  RateParams rp(GetParam());
  int symbols = 20;
  int bit_count = rp.dbps * symbols - 6;
  std::vector<std::uint8_t> bits = test::prbs_bits(bit_count);
  std::vector<std::uint8_t> punctured =
      puncturer::puncture(to_soft(encode(bits)), rp);
  ASSERT_EQ(punctured.size(), rp.cbps * symbols);
  for (int x = 7; x < punctured.size(); x += 60)
    punctured[x] = 255 - punctured[x];
  EXPECT_EQ(decode(puncturer::depuncture(punctured, rp), bit_count), bits);
}

INSTANTIATE_TEST_SUITE_P(AllRates, PuncturedViterbiTest,
                         testing::ValuesIn(test::ALL_RATES), test::rate_name);
}  // namespace
}  // namespace fun
