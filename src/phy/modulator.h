/*! \file modulator.h
 *  \brief Maps coded bits to constellation points, and points to soft bits.
 *
 *  Each subcarrier carries one complex point per symbol, and the point's
 *  position carries 1 to 6 bits (802.11a-1999 section 17.3.5.7). The first
 *  half of a subcarrier's bits pick the real part (I) and the second half the
 *  imaginary part (Q), so each axis is a 1D constellation:
 *
 *  ~~~
 *  BPSK, 1 bit, I only:           0                 1
 *                                -1        ┼       +1
 *
 *  QPSK, 1 bit per axis:          0                 1
 *                                -1        ┼       +1
 *
 *  16-QAM, 2 bits per axis:  00      01         11      10
 *                            -3      -1    ┼    +1      +3
 *
 *  64-QAM, 3 bits per axis:  000 001 011 010   110 111 101 100
 *                             -7  -5  -3  -1 ┼ +1  +3  +5  +7
 *  ~~~
 *
 *  The levels are Gray coded: neighbours differ in one bit, so the likely
 *  error, landing on a neighbour, costs one bit. They're scaled by 1, 1/√2,
 *  1/√10 and 1/√42 so every modulation has an average power of 1. More bits
 *  per point means the points are closer together, so less noise causes an
 *  error.
 *
 *  Demodulating gives a soft bit for each bit: 0 is a confident 0 and 255 a
 *  confident 1, from how far the point is from the nearest decision boundary.
 *  The Viterbi decoder uses the confidence (see viterbi.h).
 *
 *  The work is done by QAM (qam.h), one axis at a time.
 */

#ifndef MODULATOR_H
#define MODULATOR_H

#include <complex>
#include <cstdint>

#include "rates.h"

namespace fun {

/*!
 * \brief The modulator class
 *
 *  The modulator takes the input data in bits and converts it to
 *  complex doubles representing the digital modulation symbols and vice versa.
 *  Supported Modulations are:
 *  -BPSK
 *  -QPSK
 *  -16 QAM
 *  -64 QAM
 */
class modulator {
 public:
  /*!
   * \brief Modulates the data.
   * \param data Vector of data in bytes to be modulated.
   * \param rate PHY transmission rate from which the type of modulation is
   * extracted.
   * \return Vector of modulated data as complex doubles.
   */
  static std::vector<std::complex<double>> modulate(
      std::vector<std::uint8_t> data, Rate rate);

  /*!
   * \brief Demodulates the data.
   * \param data Vector of data to be demodulated in complex doubles.
   * \param rate PHY transmission frate from which the type of modulation is
   * extracted.
   * \return Vector of demodulated data in bytes.
   */
  static std::vector<std::uint8_t> demodulate(
      std::vector<std::complex<double>> data, Rate rate);
};
}  // namespace fun

#endif  // MODULATOR_H
