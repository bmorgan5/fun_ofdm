/*! \file scrambler.h
 *  \brief Scrambles the data bits, and back.
 *
 *  The scrambler XORs the data with a pseudo-random sequence, so long runs of
 *  identical bits don't turn into a few strong tones (802.11a-1999 section
 *  17.3.5.4). The sequence comes from a 7-bit shift register with the
 *  generator polynomial x^7 + x^4 + 1. Each step outputs x7 XOR x4, and
 *  shifts that output in as the new x1:
 *
 *  ~~~
 *        ┌────┬────┬────┬────┬────┬────┬────┐
 *    ┌──►│ x1 │ x2 │ x3 │ x4 │ x5 │ x6 │ x7 │
 *    │   └────┴────┴────┴─┬──┴────┴────┴─┬──┘
 *    │                    │              │
 *    │                    └─────► ⊕ ◄────┘
 *    │                            │
 *    └────────────────────────────┴─────► output
 *  ~~~
 *
 *  The sequence repeats every 127 steps. XORing it in twice gives back the
 *  data, so descrambling is the same operation.
 *
 *  Unlike 802.11a:
 *  - The register always starts at 0x5D (1011101). 802.11a starts it at a
 *    pseudo-random nonzero state for each frame, and the receiver works out
 *    the state from the first 7 bits of SERVICE, which are zeros before
 *    scrambling.
 *  - It steps once per byte, not once per bit, and only flips the lowest bit
 *    of each byte. 802.11a XORs every bit with the next output.
 */

#ifndef SCRAMBLER_H
#define SCRAMBLER_H

#include <vector>

namespace fun {
/*!
 * \brief The scrambler class
 *
 *  Scrambles the data before it's convolutionally encoded, and descrambles it
 *  after it's decoded.
 */
class scrambler {
 public:
  /*! \brief The state the shift register starts in, x7 to x1. */
  static constexpr int SEED = 0x5D;

  /*!
   * \brief Scrambles the data, or descrambles it: they're the same operation.
   * \param data The bytes to scramble.
   * \return The scrambled bytes, as many as there are in data.
   */
  static std::vector<unsigned char> scramble(std::vector<unsigned char> data);
};
}  // namespace fun

#endif  // SCRAMBLER_H
