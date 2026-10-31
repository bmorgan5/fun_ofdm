/*! \file timing_sync.h
 *  \brief Receiver block 2: finds the exact start of the frame, and corrects
 *  the frequency offset.
 *
 *  **Timing.** After an STS_END tag, this block correlates the next 160
 *  samples against the known 64-sample long training symbol (see
 *  preamble.h). The two copies of it give two peaks 64 samples apart, which
 *  pin down where each one starts. It tags LTS1 and LTS2 8 samples before the
 *  two symbols, and fft_symbols counts every later symbol from there:
 *
 *  ~~~
 *            LTS1 (8 early)  LTS2 (8 early)
 *              ▼               ▼
 *  ... CP: 32 │   LTS: 64     │   LTS: 64     │ CP: 16 │ SIGNAL: 64 │ ...
 *  ~~~
 *
 *  Starting each 64-sample window 8 samples into the cyclic prefix gives
 *  margin if the estimate is a little late. An early window is a circular
 *  shift of the symbol, which the FFT turns into a phase that rises across the
 *  subcarriers, and channel_est measures and removes that with the rest of the
 *  channel.
 *
 *  **Frequency offset.** The two radios' oscillators never quite agree, so
 *  the received signal rotates a little more with every sample. The two LTS
 *  copies are identical, so the phase between them, divided by 64, is the
 *  rotation per sample, and the block undoes it on every sample until the
 *  next frame.
 *
 *  \bug The loop that compares the two LTS copies never runs, so the
 *  estimated offset is always 0, and only a constant phase is applied.
 *  channel_est and phase_tracker correct some of the offset instead.
 */

#ifndef TIMING_SYNC_H
#define TIMING_SYNC_H

#define LTS_CORR_THRESHOLD 0.9
#define CARRYOVER_LENGTH 160
#define LTS_LENGTH 64

#include <complex>

#include "block.h"
#include "tagged_vector.h"

namespace fun {
/*!
 * \brief The timing_sync block.
 *
 * Inputs tagged samples from the frame_detector block.
 * Outputs tagged samples to the fft_symbols block.
 *
 * The timing sync block is in charge of using the two LTS symbols to align the
 * received frame in time. It also uses the two LTS symbols to perform an
 * initial frequency offset estimation and applying the necessary correction.
 */
class timing_sync : public fun::block<tagged_sample, tagged_sample> {
 public:
  timing_sync();  //!< Constructor for timing_sync block.

  virtual void work();  //!< Signal processing happens here.

 private:
  double m_phase_offset;  //!< The phase rotation from symbol to symbol

  double m_phase_acc;  //!< The total phase rotation for the current symbol

  /*!
   * \brief Vector for storing the last 160 samples from the input_buffer
   * and carrying them over to the next call to #work()
   */
  std::vector<tagged_sample> m_carryover;
};
}  // namespace fun

#endif  // TIMING_SYNC_H
