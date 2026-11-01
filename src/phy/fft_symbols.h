/*! \file fft_symbols.h
 *  \brief Receiver block 3: cuts the samples into symbols and FFTs them.
 *
 *  From each LTS1 tag, this block takes the two long training symbols (64
 *  samples each, back to back), then every following 80-sample symbol,
 *  dropping its 16-sample cyclic prefix. Each 64-sample window goes through
 *  the forward FFT, giving the symbol's 64 subcarriers:
 *
 *  ~~~
 *  │ LTS: 64 │ LTS: 64 │ CP: 16 │ SIGNAL: 64 │ CP: 16 │ data: 64 │ ...
 *     FFT       FFT      drop       FFT        drop      FFT
 *  ~~~
 *
 *  The first output symbol of each frame is tagged LTS_START.
 */

#ifndef FFT_SYMBOLS_H
#define FFT_SYMBOLS_H

#include <complex>
#include <cstddef>
#include <vector>

#include <fftw3.h>

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
  std::size_t m_offset;

  /*!
   * \brief Forward FFT
   */
  fft m_ffft;
};
}  // namespace fun

#endif  // FFT_SYMBOLS_H
