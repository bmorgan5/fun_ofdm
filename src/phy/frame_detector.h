/*! \file frame_detector.h
 *  \brief Receiver block 1: finds frames by their short training sequence.
 *
 *  The short training sequence (see preamble.h) repeats every 16 samples, so
 *  while it's arriving, each sample matches the one 16 before it. This block
 *  correlates the signal with itself 16 samples back, over a 16-sample window,
 *  and normalizes by the power:
 *
 *  ~~~
 *  corr[n] = |Σ r[m] · conj(r[m - 16])| / Σ |r[m]|²    (m over the last 16)
 *
 *   corr
 *   1.0 ┤        ┌───────────────────────┐
 *   0.9 ┤ - - - -│- - - - - - - - - - - -│- - - -  PLATEAU_THRESHOLD
 *       │        │                       │
 *     0 ┼────────┘                       └───────
 *       noise    STS arriving            LTS
 *                   ▲ STS_START             ▲ STS_END
 *            (16 samples above 0.9)   (drops below)
 *  ~~~
 *
 *  Noise doesn't repeat, so it gives a low value. A frequency offset rotates
 *  both samples alike, so it doesn't affect the magnitude, and the detector
 *  works before anything has been corrected.
 *
 *  All samples pass through. The block only adds the tags, which tell
 *  timing_sync where to look for the long training sequence.
 */

#ifndef FRAME_DETECTOR_H
#define FRAME_DETECTOR_H

// Tweakable Parameters
#define PLATEAU_THRESHOLD 0.9
#define STS_PLATEAU_LENGTH 16

#define STS_LENGTH 16

#include <complex>

#include "block.h"
#include "circular_accumulator.h"
#include "tagged_vector.h"

namespace fun {
/*!
 * \brief The frame_detector block.
 *
 * Inputs complex doubles from USRP block.
 * Outputs tagged samples to timing sync block.
 *
 * This block is in charge of detecting the beginning of a frame using the
 * short training sequence in the preamble.
 */
class frame_detector : public fun::block<std::complex<double>, tagged_sample> {
 public:
  frame_detector();  //!< Constructor for frame_detector block.

  virtual void work();  //!< Signal processing happens here.

 private:
  /*!
   * \brief Circular accumulator for calculating correlation.
   */
  circular_accumulator<std::complex<double>> m_corr_acc;

  /*!
   * \brief Circular accumulator for calculating correlation.
   */
  circular_accumulator<double> m_power_acc;

  /*!
   * \brief Counter for keeping track of STS plateau length.
   */
  int m_plateau_length;

  /*!
   * \brief Flag for signaling whether we are currently in a
   * plateau or not.
   */
  bool m_plateau_flag;

  /*!
   * \brief Vector for storing the last 16 samples from the input_buffer
   * and carrying them over to the next call to #work()
   */
  std::vector<std::complex<double>> m_carryover;
};
}  // namespace fun

#endif  // FRAME_DETECTOR_H
