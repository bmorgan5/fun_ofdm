/*! \file workload.cpp
 * \brief C++ file for the standard workloads that the benchmarks receive.
 */

#include "workload.h"

#include <algorithm>
#include <cmath>

#include "src/framing.h"

namespace fun {
/*!
 * \brief The most idle samples generated at a time, so that W-idle, which is
 * all idle, isn't generated in one piece.
 */
static const std::size_t MAX_IDLE_PART = 65536;

std::size_t workload::samples() const {
  return static_cast<std::size_t>(std::llround(seconds * sample_rate));
}

std::vector<workload> standard_workloads() {
  return {
      {.name = "W-idle", .snr_db = 20, .seed = WORKLOAD_SEED},
      {.name = "W-6",
       .rate = RATE_1_2_BPSK,
       .airtime = 0.5,
       .snr_db = 20,
       .seed = WORKLOAD_SEED},
      {.name = "W-54",
       .rate = RATE_3_4_QAM64,
       .airtime = 0.9,
       .snr_db = 30,
       .seed = WORKLOAD_SEED},
  };
}

/*!
 * Every frame has the same rate and payload size, so the same number of
 * samples, which a first frame, built and thrown away, gives.
 */
workload_source::workload_source(const workload& spec)
    : m_spec(spec),
      m_data(std::nullopt, spec.seed),
      m_channel(channel_params{.snr_db = spec.snr_db}, spec.sample_rate,
                spec.seed),
      m_frame_samples(0),
      m_idle_samples(0),
      m_remaining(spec.samples()) {
  m_channel.set_signal_power(OFDM_SIGNAL_POWER);
  if (!spec.rate) return;

  std::vector<std::uint8_t> payload(spec.payload_bytes);
  m_frame_samples = m_builder.build_frame(payload, *spec.rate).size();
  m_idle_samples = static_cast<std::size_t>(
      std::llround(static_cast<double>(m_frame_samples) * (1 - spec.airtime) /
                   spec.airtime));
}

std::uint64_t workload_source::whole_frames() const {
  if (!m_frame_samples) return 0;
  return m_spec.samples() / (m_idle_samples + m_frame_samples);
}

std::vector<std::complex<double>> workload_source::read(
    std::size_t max_samples) {
  std::vector<std::complex<double>> out;
  out.reserve(std::min(max_samples, m_remaining));
  while (out.size() < max_samples && m_remaining) {
    if (m_pending_start == m_pending.size()) {
      m_pending = next_part();
      m_pending_start = 0;
    }
    std::size_t count = std::min({max_samples - out.size(), m_remaining,
                                  m_pending.size() - m_pending_start});
    out.insert(out.end(), m_pending.begin() + m_pending_start,
               m_pending.begin() + m_pending_start + count);
    m_pending_start += count;
    m_remaining -= count;
  }
  return out;
}

std::vector<std::complex<double>> workload_source::next_part() {
  if (!m_spec.rate) return m_channel.idle(std::min(MAX_IDLE_PART, m_remaining));

  if (m_idle_next) {
    m_idle_next = false;
    if (m_idle_samples) return m_channel.idle(m_idle_samples);
  }
  m_idle_next = true;

  std::vector<std::uint8_t> payload(m_spec.payload_bytes);
  for (std::size_t i = 0; i < FRAME_HEADER_SIZE; i++)
    payload[i] = static_cast<std::uint8_t>(m_sequence >> (8 * (3 - i)));
  m_data.read(payload.data() + FRAME_HEADER_SIZE,
              payload.size() - FRAME_HEADER_SIZE);
  m_sequence++;
  return m_channel.transmit(m_builder.build_frame(payload, *m_spec.rate));
}
}  // namespace fun
