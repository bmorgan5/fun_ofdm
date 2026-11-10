/*! \file pipeline.h
 * \brief The receiver chain's six blocks, run one after the other on one
 * thread, so the benchmarks can capture each block's input.
 */

#ifndef PIPELINE_H
#define PIPELINE_H

#include <complex>
#include <cstdint>
#include <vector>

#include "src/phy/channel_est.h"
#include "src/phy/fft_symbols.h"
#include "src/phy/frame_decoder.h"
#include "src/phy/frame_detector.h"
#include "src/phy/phase_tracker.h"
#include "src/phy/timing_sync.h"

namespace fun {
/*!
 * \brief The blocks of receiver_chain, without its threads.
 *
 * step() does what receiver_chain::process_samples() does, with the blocks
 * run in turn instead of at once: each works on its input buffer, then the
 * buffers are swapped along the chain in the same way. So each block gets
 * exactly the batches it gets in receiver_chain, including the old ones that
 * receiver_chain's empty-batch \\bug hands it again.
 */
class pipeline {
 public:
  frame_detector detector;  //!< Block 1
  timing_sync sync;         //!< Block 2
  fft_symbols fft;          //!< Block 3
  channel_est equalizer;    //!< Block 4
  phase_tracker tracker;    //!< Block 5
  frame_decoder decoder;    //!< Block 6

  /*!
   * \brief Runs one batch of samples through the blocks, like
   * receiver_chain::process_samples().
   * \param samples The samples
   * \return The payloads that frame_decoder output in this step.
   */
  std::vector<std::vector<std::uint8_t>> step(
      std::vector<std::complex<double>> samples) {
    detector.input_buffer.swap(samples);

    detector.work();
    sync.work();
    fft.work();
    equalizer.work();
    tracker.work();
    decoder.work();

    sync.input_buffer.swap(detector.output_buffer);
    fft.input_buffer.swap(sync.output_buffer);
    equalizer.input_buffer.swap(fft.output_buffer);
    tracker.input_buffer.swap(equalizer.output_buffer);
    decoder.input_buffer.swap(tracker.output_buffer);
    return decoder.output_buffer;
  }
};
}  // namespace fun

#endif  // PIPELINE_H
