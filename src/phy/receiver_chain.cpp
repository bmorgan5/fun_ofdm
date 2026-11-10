/*! \file receiver_chain.cpp
 *  \brief C++ file for Receiver Chain class.
 *
 *  The Receiver Chain class is the main controller for the blocks that are
 *  used to receive and decode PHY layer frames. It holds the instances of each
 * block and shifts the data through the receive chain as it is processed
 * eventually returning the correctly received payloads (MPDUs) which can then
 * be passed to the upper layers.
 */

#include "receiver_chain.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>

#include "src/metrics/tracing.h"

namespace fun {
/*!
 * -Initializes each receiver chain block:
 *  + frame_detector
 *  + timing_sync
 *  + fft_symbols
 *  + channel_est
 *  + phase_tracker
 *  + frame_decoder
 *
 *  Adds each block to the receiver chain.
 */
receiver_chain::receiver_chain() {
  m_frame_detector = new frame_detector();
  m_timing_sync = new timing_sync();
  m_fft_symbols = new fft_symbols();
  m_channel_est = new channel_est();
  m_phase_tracker = new phase_tracker();
  m_frame_decoder = new frame_decoder();

  // We use semaphore references, so we don't
  // want them to move to a different memory location
  // if the vectors get resized
  m_wake_sems.reserve(100);
  m_done_sems.reserve(100);
  // Likewise the metrics, which the threads use while blocks are added
  m_block_metrics.reserve(100);
  m_batch_arrival.reserve(100);
  m_last_start.reserve(100);

  // Add the blocks to the receiver chain
  add_block(m_frame_detector);
  add_block(m_timing_sync);
  add_block(m_fft_symbols);
  add_block(m_channel_est);
  add_block(m_phase_tracker);
  add_block(m_frame_decoder);
}

/*!
 * Requests a stop on each block thread and then wakes it up so that it sees the
 * request and returns. The threads are joined before the semaphores and blocks
 * they use are destroyed.
 */
receiver_chain::~receiver_chain() {
  for (auto& thread : m_threads) thread.request_stop();
  for (auto& sem : m_wake_sems) sem_post(&sem);
  m_threads.clear();  // Joins the threads

  for (auto& sem : m_wake_sems) sem_destroy(&sem);
  for (auto& sem : m_done_sems) sem_destroy(&sem);

  delete m_frame_detector;
  delete m_timing_sync;
  delete m_fft_symbols;
  delete m_channel_est;
  delete m_phase_tracker;
  delete m_frame_decoder;
}

/*!
 * The #add_block function creates a wake & done semaphore for each block.
 * It then creates a new thread for the block to run in and adds that thread
 * to the thread vector for reference.
 */
void receiver_chain::add_block(fun::block_base* block) {
  m_wake_sems.push_back(sem_t());
  m_done_sems.push_back(sem_t());
  m_block_metrics.push_back({.name = block->name});
  m_batch_arrival.push_back(0);
  m_last_start.push_back(0);
  std::size_t index = m_wake_sems.size() - 1;
  sem_init(&m_wake_sems[index], 0, 0);
  sem_init(&m_done_sems[index], 0, 0);
  m_threads.push_back(std::jthread([this, index, block](std::stop_token stop) {
    run_block(stop, index, block);
  }));
}

/*!
 * The #run_block function is the main thread for controlling the calls to
 * each block's work function. This function loops until a stop is requested. It
 * first waits for the wake_sempahore to post indicating its time for the block
 * to "wake up" and process the data that has just been placed in its
 * input_buffer by running its work() function. Then once, the work() function
 * returns run_block posts to the done sempahore that the block has finished
 * processing everything in the input_buffer. At this point it loops back around
 * and waits for the block to be "woken up" again when the next set of input
 * data is ready.
 *
 * Around each call to work(), it records the block's times in its own
 * block_metrics, and a slice in the trace.
 */
void receiver_chain::run_block(std::stop_token stop, std::size_t index,
                               fun::block_base* block) {
  trace::name_thread(block->name);
  while (1) {
    sem_wait(&m_wake_sems[index]);
    if (stop.stop_requested()) return;

    metrics::time_point start = metrics::now();
    block->work();
    metrics::time_point end = metrics::now();

    if constexpr (metrics::ENABLED) {
      block_metrics& times = m_block_metrics[index];
      times.wait.record(start - m_ready);
      times.processing.record(end - start);
      if (m_last_start[index])
        times.start_interval.record(start - m_last_start[index]);
      m_last_start[index] = start;
      if (m_batch_arrival[index])
        times.lag.record(end - m_batch_arrival[index]);
      // Block `index` works on the batch from `index` calls ago
      trace::slice(block->name.c_str(), start, end,
                   {{"batch", static_cast<std::int64_t>(m_calls - index)}});
    }

    sem_post(&m_done_sems[index]);
  }
}

/*!
 * This function is the main scheduler for the receive chain. It takes in raw
 * complex samples from the usrp block and passes them first into the Frame
 * Detector block's input buffer. It then unlocks each of the threads by posting
 * to each block's "wake" semaphore. It then waits for each thread to post that
 * it is done with that call to its work() function. Once all the threads are
 * done it shifts the contents of each blocks output buffer to the input buffer
 * of the next block in the chain and returns the contents of the Frame
 * Decoder's output buffer.
 */
std::vector<std::vector<std::uint8_t>> receiver_chain::process_samples(
    std::vector<std::complex<double>> samples, metrics::time_point arrival) {
  if constexpr (metrics::ENABLED) {
    if (m_last_arrival) m_arrival_interval.record(arrival - m_last_arrival);
    m_last_arrival = arrival;
    m_arrivals[m_calls % m_arrivals.size()] = {
        .first_sample = m_samples, .size = samples.size(), .time = arrival};
    for (std::size_t k = 0; k < m_batch_arrival.size(); k++) {
      m_batch_arrival[k] =
          m_calls >= k ? m_arrivals[(m_calls - k) % m_arrivals.size()].time : 0;
    }
    trace::instant("arrival", arrival,
                   {{"batch", static_cast<std::int64_t>(m_calls)},
                    {"samples", static_cast<std::int64_t>(samples.size())}});
  }
  m_samples += samples.size();

  // samples -> sync short in
  m_frame_detector->input_buffer.swap(samples);

  m_ready = metrics::now();
  trace::instant("ready", m_ready,
                 {{"batch", static_cast<std::int64_t>(m_calls)}});

  // Unlock the threads
  for (std::size_t x = 0; x < m_wake_sems.size(); x++)
    sem_post(&m_wake_sems[x]);

  // Wait for the threads to finish
  for (std::size_t x = 0; x < m_done_sems.size(); x++)
    sem_wait(&m_done_sems[x]);

  // The decoder's output is only this call's if it had input: with none, it
  // leaves its last output there (see the \\bug in receiver_chain.h)
  bool new_output = !m_frame_decoder->input_buffer.empty();

  // Update the buffers
  m_timing_sync->input_buffer.swap(m_frame_detector->output_buffer);
  m_fft_symbols->input_buffer.swap(m_timing_sync->output_buffer);
  m_channel_est->input_buffer.swap(m_fft_symbols->output_buffer);
  m_phase_tracker->input_buffer.swap(m_channel_est->output_buffer);
  m_frame_decoder->input_buffer.swap(m_phase_tracker->output_buffer);

  if (metrics::ENABLED && new_output) {
    metrics::time_point out = metrics::now();
    for (std::uint64_t end : m_frame_decoder->frame_ends)
      record_latency(end, out);
  }
  m_calls++;

  // Return any completed packets
  return m_frame_decoder->output_buffer;
}

/*!
 * timing_sync passes every sample on CARRYOVER_LENGTH samples late, so a
 * position in fft_symbols' input is that many samples later than in the
 * stream that process_samples() is given.
 */
void receiver_chain::record_latency(std::uint64_t last_sample,
                                    metrics::time_point out) {
  if (last_sample < CARRYOVER_LENGTH) return;
  std::uint64_t sample = last_sample - CARRYOVER_LENGTH;
  for (const arrival& batch : m_arrivals) {
    if (batch.size && batch.first_sample <= sample &&
        sample < batch.first_sample + batch.size) {
      m_latency.record(out - batch.time);
      trace::instant("payload", out, {{"latency_ns", out - batch.time}});
      return;
    }
  }
}

chain_metrics receiver_chain::metrics() const {
  chain_metrics result = {
      .arrival_interval = m_arrival_interval,
      .latency = m_latency,
      .blocks = m_block_metrics,
      .frames_detected = m_frame_detector->detections(),
      .sync_lost = m_timing_sync->sync_lost(),
      .signal_failed = m_frame_decoder->signal_failed(),
      .crc_ok = m_frame_decoder->crc_ok(),
      .crc_failed = m_frame_decoder->crc_failed(),
  };
  return result;
}

}  // namespace fun
