/*! \file receiver.cpp
 *  \brief C++ file for the receiver class.
 *
 *  The receiver class is the public interface for the fun_ofdm receiver.
 *  This is the easiest way to start receiving 802.11a OFDM frames out of the
 * box.
 */

#include "receiver.h"

#include <cstdint>

#include "src/metrics/tracing.h"

namespace fun {

receiver::receiver(
    void (*callback)(std::vector<std::vector<std::uint8_t>> packets),
    usrp_params params)
    : m_callback(callback),
      m_usrp(params),
      m_rec_chain(),
      m_samples(NUM_RX_SAMPLES) {}

/*!
 *  This function loops until a stop is requested, pulling samples from the USRP
 * and passing them through the receiver chain. It then passes any successfully
 * decoded packets to the callback function for the user to process further.
 */
void receiver::run(std::stop_token stop) {
  while (!stop.stop_requested()) {
    metrics::time_point start = metrics::now();
    rx_metadata batch = m_usrp.get_samples(NUM_RX_SAMPLES, m_samples);
    metrics::time_point arrival = metrics::now();
    trace::slice("recv", start, arrival);

    if (batch.overflow) {
      m_overflows++;
      trace::instant("overflow", arrival);
    }
    if (metrics::ENABLED && batch.device_time) {
      std::int64_t offset = arrival - *batch.device_time;
      if (!m_min_offset || offset < *m_min_offset) m_min_offset = offset;
      m_recv_delay.record(offset - *m_min_offset);
    }

    std::vector<std::vector<std::uint8_t>> packets =
        m_rec_chain.process_samples(m_samples, arrival);

    m_callback(packets);
  }
}
}  // namespace fun
