/*! \file interleaver.h
 *  \brief Reorders coded bits so neighbouring bits go on distant subcarriers.
 *
 *  Echoes make some subcarriers fade (see README.md). Without interleaving, a
 *  faded subcarrier would wipe out a run of consecutive coded bits, and the
 *  Viterbi decoder can't fix bursts like that. The interleaver spreads
 *  neighbouring coded bits across the symbol, so a fade turns into scattered
 *  errors that the decoder fixes easily (802.11a-1999 section 17.3.5.6).
 *
 *  The first permutation writes a symbol's bits in rows of 16 and reads them
 *  out by column. For BPSK, with 48 bits per symbol:
 *
 *  ~~~
 *  written in rows:   0  1  2  3 ... 15
 *                    16 17 18 19 ... 31
 *                    32 33 34 35 ... 47
 *
 *  read by column:    0 16 32  1 17 33  2 18 34 ... 15 31 47
 *  ~~~
 *
 *  Bits k and k + 1 end up 3 positions apart, so on subcarriers 3 apart. The
 *  standard's second permutation, for QAM, alternates which bits of each
 *  subcarrier get the more and less reliable positions in the constellation.
 *  BitInterleave computes both for any number of bits per subcarrier.
 *
 *  Unlike 802.11a, interleave() and deinterleave() always use the BPSK
 *  permutation, on each block of 48 bits, whatever the modulation.
 */

#ifndef INTERLEAVER_H
#define INTERLEAVER_H

#include <stdlib.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "rates.h"

namespace fun {

/*!
 * \brief The interleaver class
 *
 * The interleaver class performs interleaving as described in section 17.3.5.6
 * of the 802.11a-1999 standard. The Interleaver class contains two static
 * functions: interleave and deinterleave and thus doesn't need a constructor.
 * However, it does use the BitInterleave struct as a helper for these two
 * functions.
 */
class interleaver {
 public:
  /*!
   * \brief interleaves the data
   * \param data Vector of data to be interleaved
   * \return Vector of interleaved data
   */
  static std::vector<std::uint8_t> interleave(std::vector<std::uint8_t> data);

  /*!
   * \brief deinterleaves the data
   * \param data Vector of data to be deinterleaved
   * \return Vector of deinterleaved data
   */
  static std::vector<std::uint8_t> deinterleave(std::vector<std::uint8_t> data);
};

/*!
 * \brief The BitInterleave struct
 *
 * This struct sets up the interleaving map based on the the number of coded
 * bits per symbol
 */
struct BitInterleave {
  std::size_t d_bpsc;  // coded bits per subcarrier
  std::size_t d_cbps;  // coded bits per symbol
  static constexpr std::size_t d_num_chunks = 16;

  BitInterleave(std::size_t ncarriers, std::size_t nbits)
      : d_bpsc(nbits), d_cbps(nbits * ncarriers) {}

  std::size_t index(std::size_t k) {
    // see 17.3.5.6 in 802.11a-1999, floor is implicit
    assert(k < d_cbps);
    std::size_t s = std::max(d_bpsc / 2, std::size_t{1});
    std::size_t i =
        (d_cbps / d_num_chunks) * (k % d_num_chunks) + (k / d_num_chunks);
    std::size_t j =
        s * (i / s) + (i + d_cbps - (d_num_chunks * i / d_cbps)) % s;
    assert(j < d_cbps);
    return j;
  }

  void fill(std::vector<std::size_t>& v, bool inverse) {
    v.resize(d_cbps);
    if (!inverse) {
      for (std::size_t i = 0; i < d_cbps; ++i) {
        v[i] = index(i);
      }
    } else {
      for (std::size_t i = 0; i < d_cbps; ++i) {
        v[index(i)] = i;
      }
    }
  }
};
}  // namespace fun

#endif  // INTERLEAVER_H
