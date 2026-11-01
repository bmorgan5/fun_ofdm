/*! \file puncturer.h
 *  \brief Punctures rate 1/2 coded bits to rate 2/3 or 3/4, and back.
 *
 *  Puncturing deletes coded bits in a fixed pattern (802.11a-1999 section
 *  17.3.5.5). The data takes fewer coded bits, so more of it fits in each
 *  symbol, in exchange for less protection:
 *
 *  ~~~
 *  rate 1/2 coded bits:  A0 B0 A1 B1 A2 B2 | A3 B3 ...
 *  rate 3/4 sends:       A0 B0    B1    B2 |          4 of every 6
 *
 *  rate 1/2 coded bits:  A0 B0 A1 B1 | A2 B2 ...
 *  rate 2/3 sends:       A0    A1 B1 |                3 of every 4
 *  ~~~
 *
 *  Depuncturing puts the received bits back in place and fills each hole with
 *  127, the soft bit for "no idea", halfway between a 0 (0) and a 1 (255). The
 *  Viterbi decoder works out the missing bits from the ones around them.
 *
 *  Unlike 802.11a, which sends A0 B0 A1 B2 for rate 3/4 and A0 B0 A1 for rate
 *  2/3, the patterns are the ones above.
 */

#ifndef PUNCTURER_H
#define PUNCTURER_H

#include <cstdint>

#include "rates.h"

namespace fun {
/*!
 * \brief The puncturer class
 *
 *  The puncturer class punctures the convolutionally encoded data to
 *  effectively increase the coding rate.  On the receive side it
 *  depunctures the received punctured data by inserting 0's into the
 *  "puncture holes".
 */
class puncturer {
 public:
  /*!
   * \brief Punctures the convolutionally encoded data to get it to the desired
   * coding rate.
   * \param data Vector of the convolutionally encoded data to be punctured.
   * \param rate_params The parameters for the PHY Rate from which the coding
   * rate is extracted.
   * \return Vector of the punctured data.
   */
  static std::vector<std::uint8_t> puncture(std::vector<std::uint8_t> data,
                                            RateParams rate_params);

  /*!
   * \brief depunctures the data by inserting 0's in the "puncture holes"
   * \param data Vector of the punctured data to be depunctured.
   * \param rate_params The parameters for the PHY Rate from which the coding
   * rate is extracted.
   * \return Vector of the depunctured data.
   */
  static std::vector<std::uint8_t> depuncture(std::vector<std::uint8_t> data,
                                              RateParams rate_params);
};
}  // namespace fun

#endif  // PUNCTURER_H
