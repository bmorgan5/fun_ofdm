/*! \file test_util.h
 *  \brief Helpers shared by the PHY tests.
 */

#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <gtest/gtest.h>

#include <algorithm>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "block.h"
#include "rates.h"

namespace fun::test {
/*! \brief Every PHY rate, for tests that run at each one. */
inline const std::vector<Rate> ALL_RATES = {
    RATE_1_2_BPSK,  RATE_2_3_BPSK,  RATE_3_4_BPSK,  RATE_1_2_QPSK,
    RATE_2_3_QPSK,  RATE_3_4_QPSK,  RATE_1_2_QAM16, RATE_2_3_QAM16,
    RATE_3_4_QAM16, RATE_2_3_QAM64, RATE_3_4_QAM64};

/*! \brief Names a test parameterized by rate, i.e. "3_4_QAM16". */
inline std::string rate_name(const testing::TestParamInfo<Rate>& info) {
  std::string name = RateParams(info.param).name;
  for (char& c : name) {
    if (c == '/' || c == ' ') c = '_';
  }
  return name;
}

/*!
 * \brief Generates the PRBS-15 test sequence.
 *
 * The polynomial is x^15 + x^14 + 1, as in ITU-T O.150, and the register starts
 * at all ones. The sequence repeats every 32767 bits and contains every nonzero
 * 15-bit pattern once per period. Unlike the standard library's random
 * distributions, it's fully specified, so the tests use the same data with any
 * compiler or library.
 */
class prbs15 {
 public:
  /*! \brief Returns the next bit (0 or 1). */
  std::uint8_t next_bit() {
    std::uint8_t bit = ((m_state >> 14) ^ (m_state >> 13)) & 1;
    m_state = ((m_state << 1) | bit) & 0x7FFF;
    return bit;
  }

 private:
  std::uint16_t m_state = 0x7FFF;  //!< The 15-bit shift register
};

/*!
 * \brief Returns count bits of PRBS-15, one per byte (0 or 1), starting offset
 * bits into the sequence. Different offsets give different test data.
 */
inline std::vector<std::uint8_t> prbs_bits(std::size_t count,
                                           std::size_t offset = 0) {
  prbs15 prbs;
  for (std::size_t x = 0; x < offset; x++) prbs.next_bit();
  std::vector<std::uint8_t> bits(count);
  for (std::uint8_t& b : bits) b = prbs.next_bit();
  return bits;
}

/*!
 * \brief Returns count bytes of PRBS-15, packed MSB first, starting offset bits
 * into the sequence.
 */
inline std::vector<std::uint8_t> prbs_bytes(std::size_t count,
                                            std::size_t offset = 0) {
  std::vector<std::uint8_t> bits = prbs_bits(8 * count, offset);
  std::vector<std::uint8_t> bytes(count, 0);
  for (std::size_t x = 0; x < bits.size(); x++)
    bytes[x / 8] |= bits[x] << (7 - x % 8);
  return bytes;
}

/*!
 * \brief Returns count samples of low-level noise: QPSK points of the given
 * amplitude on each axis, from PRBS-15 starting offset bits into the sequence.
 */
inline std::vector<std::complex<double>> prbs_noise(std::size_t count,
                                                    double amplitude,
                                                    std::size_t offset = 0) {
  std::vector<std::uint8_t> bits = prbs_bits(2 * count, offset);
  std::vector<std::complex<double>> noise(count);
  for (std::size_t x = 0; x < count; x++) {
    noise[x] = {bits[2 * x] ? amplitude : -amplitude,
                bits[2 * x + 1] ? amplitude : -amplitude};
  }
  return noise;
}

/*!
 * \brief Runs a receiver block on input, batch items per call to work(), and
 * returns its outputs from every call, in order. The last batch can be
 * shorter.
 */
template <typename I, typename O>
std::vector<O> run_block(block<I, O>& b, const std::vector<I>& input,
                         std::size_t batch) {
  std::vector<O> output;
  for (std::size_t x = 0; x < input.size(); x += batch) {
    std::size_t end = std::min(input.size(), x + batch);
    b.input_buffer.assign(input.begin() + x, input.begin() + end);
    b.work();
    output.insert(output.end(), b.output_buffer.begin(), b.output_buffer.end());
  }
  return output;
}
}  // namespace fun::test

#endif  // TEST_UTIL_H
