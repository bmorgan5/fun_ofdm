/*! \file receiver_chain.h
 *  \brief The receiver: six blocks in a pipeline, from samples to payloads.
 *
 *  ~~~
 *  samples → frame_detector → timing_sync → fft_symbols
 *          → channel_est → phase_tracker → frame_decoder → payloads
 *  ~~~
 *
 *  Each block runs on its own thread. A call to process_samples() starts all
 *  six on their input buffers at once and waits for them, then moves each
 *  block's output to the next block's input. So a chunk of samples moves one
 *  block on per call, and its payloads come out five calls after it went in:
 *
 *  ~~~
 *  call              1        2        3        4        5        6
 *  frame_detector    chunk 1  chunk 2  chunk 3  chunk 4  chunk 5  chunk 6
 *  timing_sync                chunk 1  chunk 2  chunk 3  chunk 4  chunk 5
 *  fft_symbols                         chunk 1  chunk 2  chunk 3  chunk 4
 *  channel_est                                  chunk 1  chunk 2  chunk 3
 *  phase_tracker                                         chunk 1  chunk 2
 *  frame_decoder                                                  chunk 1
 *  ~~~
 *
 *  The blocks keep state between calls, so a frame can span chunks. They mark
 *  the frame's parts with tags on the samples (see tagged_vector.h).
 *
 *  **Metrics.** The chain times every call (src/metrics), and metrics()
 *  returns what it measured:
 *
 *  ~~~
 *  arrival ── ready ──┬── start ── work() ── end      (each block's thread)
 *                     │   ◄wait►  ◄processing►
 *                     │   ◄── start interval: from the last start ──►
 *    ◄──────────── lag: the batch's arrival → end ──────────────►
 *
 *  a frame's last sample arrives ··· 5 calls later ··· its payload comes out
 *    ◄────────────────────────── latency ─────────────────────────►
 *  ~~~
 *
 *  - **Arrival:** when the batch arrived, which the caller passes to
 *    process_samples(): when the USRP returned it, for rx.
 *  - **Ready:** when process_samples() hands the blocks their inputs.
 *  - **Lag:** block k works on the batch that arrived k calls ago, so its lag
 *    is how far it is behind the samples' arrival. The largest is the
 *    backlog.
 *  - **Latency:** from the arrival of the batch holding a frame's last sample
 *    to process_samples() returning its payload. fft_symbols marks each
 *    symbol with its last sample's position, so the decoder knows where each
 *    frame ends. The symbols start 8 samples early (timing_sync.h), so this
 *    is the frame's last sample but 8.
 *
 *  Each block's histograms belong to its thread, and the chain's own to the
 *  caller's, so recording needs no locks. The semaphores between them order
 *  the reads in metrics() after the writes.
 *
 *  \bug A block that gets an empty batch returns without clearing its output
 *  buffer. The swap left the next block's input from the call before there,
 *  which the next block has already processed, so it gets the same input
 *  again. Blocks get empty batches when process_samples() is given no
 *  samples, and in calls where fft_symbols completes no symbol, as with
 *  batches shorter than 80 samples. frame_decoder likewise outputs its old
 *  payloads again when it gets an empty batch.
 */

#ifndef RECEIVER_CHAIN_H
#define RECEIVER_CHAIN_H

#include <semaphore.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "block.h"
#include "channel_est.h"
#include "fft_symbols.h"
#include "frame_decoder.h"
#include "frame_detector.h"
#include "phase_tracker.h"
#include "rates.h"
#include "src/metrics/histogram.h"
#include "src/metrics/metrics.h"
#include "tagged_vector.h"
#include "timing_sync.h"

namespace fun {
/*!
 * \brief The times of one block's calls to work(), in nanoseconds.
 */
struct block_metrics {
  std::string name;                   //!< The block's name
  metrics::histogram wait;            //!< Input ready → work() starts
  metrics::histogram processing;      //!< work() starts → it ends
  metrics::histogram start_interval;  //!< One start → the next
  metrics::histogram lag;             //!< The batch's arrival → work() ends
};

/*!
 * \brief What a receiver_chain measured: the times in nanoseconds, and what
 * happened to the frames.
 */
struct chain_metrics {
  metrics::histogram arrival_interval;  //!< One batch's arrival → the next's
  metrics::histogram latency;  //!< A frame's last sample arrives → payload out
  std::vector<block_metrics> blocks;  //!< Each block's, in the chain's order

  std::uint64_t frames_detected = 0;  //!< frame_detector::detections()
  std::uint64_t sync_lost = 0;        //!< timing_sync::sync_lost()
  std::uint64_t signal_failed = 0;    //!< frame_decoder::signal_failed()
  //! frame_decoder::crc_ok(), by #Rate
  std::array<std::uint64_t, RATE_COUNT> crc_ok{};
  //! frame_decoder::crc_failed(), by #Rate
  std::array<std::uint64_t, RATE_COUNT> crc_failed{};
};

/*! \brief The Receiver Chain class.
 *
 *  Inputs raw complex doubles representing the base-band digitized time domain
 * signal.
 *
 *  Outputs vector of correctly received payloads (MPDUs) which are themselves
 * vectors of bytes.
 *
 *  The Receiver Chain class is the main controller for the blocks that are
 *  used to receive and decode PHY layer frames. It holds the instances of each
 * block and shifts the data through the receive chain as it is processed
 * eventually returning the correctly received payloads (MPDUs) which can then
 * be passed to the upper layers.
 */
class receiver_chain {
 public:
  /*!
   * \brief Constructor for receiver_chain
   */
  receiver_chain();

  /*!
   * \brief Destructor for receiver_chain. Stops and joins the block threads.
   */
  ~receiver_chain();

  receiver_chain(const receiver_chain&) = delete;
  receiver_chain& operator=(const receiver_chain&) = delete;

  /*!
   * \brief Processes the raw time domain samples.
   * \param samples A vector of received time-domain samples from the usrp block
   * to pass to the receive chain for signal processing.
   * \param arrival When the samples arrived, for the metrics: by default,
   * now.
   * \return A vector of correctly received payloads where each payload is its
   * own vector of bytes.
   */
  std::vector<std::vector<std::uint8_t>> process_samples(
      std::vector<std::complex<double>> samples,
      metrics::time_point arrival = metrics::now());

  /*!
   * \brief What the chain has measured so far. Call it between calls to
   * process_samples(), from the thread that calls it.
   */
  chain_metrics metrics() const;

 private:
  /**********
   * Blocks *
   **********/

  frame_detector* m_frame_detector;  //!< Detects start of frame using STS
  timing_sync*
      m_timing_sync;  //!< Aligns frame in time using LTS & some freq correction
  fft_symbols* m_fft_symbols;  //!< Forward FFT of symbols
  channel_est*
      m_channel_est;  //!< Channel estimation and equalization in freq domain
  phase_tracker* m_phase_tracker;  //!< Phase rotation tracking
  frame_decoder* m_frame_decoder;  //!< Frame decoding

  /***********************************
   * Scheduler Variables and Methods *
   ***********************************/

  /*!
   * \brief Adds block to the receiver call chain
   * \param block A pointer to the block so that its work function can be called
   */
  void add_block(fun::block_base* block);

  /*!
   * \brief Runs the block by calling its work function
   * \param stop Stop token which ends the loop once it has been woken up
   * \param index the block's index for referencing the correct semaphores for
   * that block.
   * \param block A pointer to the block used as a handle to access its work()
   * function.
   */
  void run_block(std::stop_token stop, std::size_t index,
                 fun::block_base* block);

  std::vector<std::jthread>
      m_threads;  //!< Vector of threads - one for each block

  std::vector<sem_t>
      m_wake_sems;  //!< Vector of semaphores used to "wake up" each block

  std::vector<sem_t> m_done_sems;  //!< Vector of semaphores used to determine
                                   //!< when the blocks are done

  /***********
   * Metrics *
   ***********/

  //! A batch's arrival, kept for the frames and blocks that come after it
  struct arrival {
    std::uint64_t first_sample;  //!< The position of its first sample
    std::size_t size;            //!< The number of samples
    metrics::time_point time;    //!< When it arrived
  };

  /*!
   * \brief The last batches' arrivals, by call number modulo the size, which
   * covers a frame's end and the 5 calls to its payload, even in batches of 1
   * sample.
   */
  std::array<arrival, 1024> m_arrivals{};

  std::uint64_t m_calls = 0;        //!< Calls to process_samples() so far
  std::uint64_t m_samples = 0;      //!< Samples passed to it so far
  metrics::time_point m_ready = 0;  //!< When this call's inputs were ready

  //! The arrival of the batch each block works on in this call, or 0 if
  //! there's none yet
  std::vector<metrics::time_point> m_batch_arrival;

  //! Each block's last start, or 0 before its first
  std::vector<metrics::time_point> m_last_start;

  std::vector<block_metrics> m_block_metrics;  //!< Each block's times
  metrics::histogram m_arrival_interval;       //!< See chain_metrics
  metrics::histogram m_latency;                //!< See chain_metrics
  metrics::time_point m_last_arrival = 0;      //!< The last call's arrival

  /*!
   * \brief Records a frame's latency.
   * \param last_sample Where it ends, as tagged_vector::last_sample
   * \param out When its payload came out
   */
  void record_latency(std::uint64_t last_sample, metrics::time_point out);
};

}  // namespace fun

#endif  // RECEIVER_CHAIN_H
