/*! \file fft_symbols.h
 *  \brief Header file for the FFT symbols block.
 *
 * This FFT Symbols block aligns the input samples into symbols, chops off the
 * cyclic prefixes, and performs a forward FFT on vectorized samples to convert
 * them from time domain to frequency domain symbols.
 */

#ifndef FFT_SYMBOLS_H
#define FFT_SYMBOLS_H

#include <fftw3.h>

#include <complex>
#include <vector>

#include "block.h"
#include "fft.h"
#include "tagged_vector.h"

namespace fun {
/*!
 * \brief The fft_symbols block.
 *
 * Inputs tagged_samples from timing_sync block (time domain samples).
 * Outputs tagged_vectors to channel estimator block (frequency domain samples).
 *
 * This FFT Symbols aligns the input samples into symbols, chops off the cyclic
 * prefixes, and performs a forward FFT on vectorized samples to convert them
 * from time domain to frequency domain symbols.
 */
class fft_symbols : public fun::block<tagged_sample, tagged_vector<64>> {
 public:
  fft_symbols();  //!< Constructor for fft_symbols block.

  virtual void work();  //!< Signal processing happens here.

 private:
  /*!
   * \brief Current vector being filled
   */
  tagged_vector<64> m_current_vector;

  /*!
   * \brief Offset into the current vector being filled
   */
  int m_offset;

  /*!
   * \brief Forward FFT
   */
  fft m_ffft;
};
}  // namespace fun

#endif  // FFT_SYMBOLS_H
