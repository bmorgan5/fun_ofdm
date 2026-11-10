/*! \file workload.h
 * \brief Header file for the standard workloads that the benchmarks receive.
 *
 * A workload is a stream of received samples, as a USRP would deliver them:
 * frames separated by idle time, with noise throughout. The standard ones are
 * 10 s at 20 MS/s, the sample rate of a 20 MHz channel:
 *
 * | Workload | Frames                               | Airtime | SNR   |
 * |----------|--------------------------------------|---------|-------|
 * | W-idle   | none: noise only                     | 0%      | 20 dB |
 * | W-6      | 1000 bytes at 6 Mbit/s (BPSK 1/2)    | 50%     | 20 dB |
 * | W-54     | 1000 bytes at 54 Mbit/s (64-QAM 3/4) | 90%     | 30 dB |
 *
 * A real receiver mostly hears noise, so W-idle measures the cost of the
 * detector that always runs. W-6 has long frames, and W-54 keeps the decoder
 * busiest.
 *
 * Each workload is generated from a fixed seed, #WORKLOAD_SEED, so it's the
 * same on every machine and isn't stored anywhere. It's 3.2 GB as
 * std::complex<double>, so workload_source generates it a piece at a time.
 */

#ifndef WORKLOAD_H
#define WORKLOAD_H

#include <complex>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "src/channel.h"
#include "src/payload.h"
#include "src/phy/frame_builder.h"
#include "src/phy/rates.h"

namespace fun {
/*!
 * \brief The mean power of an OFDM symbol's samples from frame_builder: 52
 * subcarriers of power 1, through the inverse FFT, which divides each sample
 * by 64. A workload's SNR is relative to it, so W-idle, which has no frames,
 * has a noise floor too.
 */
constexpr double OFDM_SIGNAL_POWER = 52.0 / (64 * 64);

/*!
 * \brief The seed of every standard workload.
 *
 * A seed is part of a workload's definition: changing it changes the
 * samples, and with them which frames fail, so it moves the baseline. The
 * workloads differ in rate, airtime and SNR, which is what changes the
 * receiver's work, so they don't need different seeds.
 */
constexpr std::uint64_t WORKLOAD_SEED = 1;

/*!
 * \brief What a workload holds.
 */
struct workload {
  std::string name;  //!< The workload's name, e.g. "W-54"

  //! The frames' rate, or std::nullopt for noise only
  std::optional<Rate> rate;

  //! Each frame's payload, in bytes, including the sequence number
  std::size_t payload_bytes = 1000;

  //! The fraction of samples that are frames, more than 0 and at most 1. The
  //! rest are idle.
  double airtime = 0;

  double snr_db = 20;  //!< The SNR, relative to #OFDM_SIGNAL_POWER

  double seconds = 10;  //!< How long the workload is

  double sample_rate = 20e6;  //!< The sample rate, in samples per second

  std::uint64_t seed = 0;  //!< The seed of the payloads and the noise

  /*!
   * \brief The number of samples in the workload.
   */
  std::size_t samples() const;
};

/*!
 * \brief The standard workloads: W-idle, W-6 and W-54, in that order.
 */
std::vector<workload> standard_workloads();

/*!
 * \brief Generates a workload's samples, in order.
 *
 * The samples repeat a pattern: the idle time, then a frame. The idle time is
 * set by the airtime, so with 50% airtime it's as long as a frame. Each
 * frame's payload is a sequence number, 4 bytes, big-endian, as in
 * framing.h, then random bytes from a random_source. The channel then adds
 * noise to everything (channel.h). The workload ends after
 * workload::samples() samples, which can cut the last frame short.
 */
class workload_source {
 public:
  /*!
   * \brief Constructor for workload_source.
   * \param spec The workload to generate
   */
  explicit workload_source(const workload& spec);

  /*!
   * \brief Generates the next samples.
   * \param max_samples The most samples to return
   * \return The next samples: \p max_samples of them, fewer at the end of the
   * workload, and none after it.
   */
  std::vector<std::complex<double>> read(std::size_t max_samples);

  /*!
   * \brief The number of frames in the workload that aren't cut short.
   */
  std::uint64_t whole_frames() const;

  /*!
   * \brief The number of samples in each frame, or 0 for noise only.
   */
  std::size_t frame_samples() const { return m_frame_samples; }

  /*!
   * \brief The number of idle samples before each frame.
   */
  std::size_t idle_samples() const { return m_idle_samples; }

 private:
  /*!
   * \brief Generates the next part of the pattern: the idle time or a frame.
   */
  std::vector<std::complex<double>> next_part();

  workload m_spec;  //!< The workload

  random_source m_data;  //!< The payloads' random bytes

  frame_builder m_builder;  //!< Builds the frames

  channel m_channel;  //!< Adds the noise

  std::size_t m_frame_samples;  //!< The samples in each frame

  std::size_t m_idle_samples;  //!< The idle samples before each frame

  std::size_t m_remaining;  //!< The samples left in the workload

  std::uint32_t m_sequence = 0;  //!< The next frame's sequence number

  bool m_idle_next = true;  //!< Whether the next part is idle time

  //! Generated samples that haven't been read yet
  std::vector<std::complex<double>> m_pending;

  std::size_t m_pending_start = 0;  //!< The first unread sample in m_pending
};
}  // namespace fun

#endif  // WORKLOAD_H
