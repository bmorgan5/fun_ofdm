/*! \file receiver.h
 * \brief Receives frames over the air with a USRP.
 *
 * This is the simplest way to receive: it reads samples from a usrp and runs
 * them through the PHY's receiver_chain, which passes the payloads it decodes
 * to a callback:
 *
 * ~~~
 * air → usrp::get_samples() → receiver_chain → callback(payloads)
 * ~~~
 */

#ifndef RECEIVER_H
#define RECEIVER_H

#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <vector>

#include "src/metrics/histogram.h"
#include "src/metrics/metrics.h"
#include "src/phy/receiver_chain.h"
#include "src/usrp/usrp.h"

/*!
 * \brief The number of samples receiver::run() reads from the USRP at a time.
 */
#define NUM_RX_SAMPLES 4096

namespace fun {

/*!
 * \brief Receives frames over the air with one USRP, and passes their payloads
 * to a callback.
 *
 * Usage: create a receiver with a callback and the USRP's parameters, then
 * call run(). It reads samples from the USRP and runs them through the receiver
 * chain on the calling thread, and calls the callback with the payloads
 * decoded from each batch of samples. It returns once a stop is requested on
 * the std::stop_token passed to it.
 *
 * The callback runs on the same thread that reads from the USRP, so a slow
 * callback delays the next read, and the USRP can overflow and drop samples.
 */
class receiver {
 public:
  /*!
   * \brief Called after each batch of samples, with the payloads that passed
   * their CRC-32 (none, often), on the thread that calls run().
   */
  using callback =
      std::function<void(std::vector<std::vector<std::uint8_t>> packets)>;

  /*!
   * \brief Starts the receiver chain's threads.
   * \param radio The USRP to read from, which must outlive the receiver. Its
   * RX stream is already running, so call run() soon after, or it overflows.
   * \param on_packets Called with each batch's payloads
   */
  receiver(usrp& radio, callback on_packets);

  /*!
   * \brief Receives frames until a stop is requested.
   * \param stop Stop token for ending the receiver. It is checked between
   * batches of #NUM_RX_SAMPLES samples, so run() returns within one batch of
   * the request.
   */
  void run(std::stop_token stop);

  //! What the receiver chain measured (receiver_chain::metrics())
  chain_metrics metrics() const { return m_rec_chain.metrics(); }

  //! The number of times the USRP overflowed and dropped samples
  std::uint64_t overflows() const { return m_overflows; }

  /*!
   * \brief How late each batch reached the host, in nanoseconds: its arrival
   * on the host's clock minus its time on the device's, less the smallest
   * such difference so far, so that the clocks' offset cancels out. It's the
   * USB or Ethernet and driver's delay above the quickest batch's.
   *
   * \bug The two clocks drift apart by up to tens of parts per million, which
   * builds up over a long run: 20 ppm is 72 ms an hour. The delay grows with
   * the drift, or, if the device's clock runs fast, the smallest difference
   * keeps falling, and the delay is too small.
   */
  const metrics::histogram& recv_delay() const { return m_recv_delay; }

 private:
  usrp& m_usrp;  //!< Reads the samples from the air

  callback m_callback;  //!< Called with the payloads from each batch

  receiver_chain m_rec_chain;  //!< Detects and decodes frames in the samples

  //! The latest batch of samples read from the USRP
  std::vector<std::complex<double>> m_samples;

  std::uint64_t m_overflows = 0;    //!< See overflows()
  metrics::histogram m_recv_delay;  //!< See recv_delay()
  //! The smallest host time minus device time so far, for recv_delay()
  std::optional<std::int64_t> m_min_offset;
};

}  // namespace fun

#endif  // RECEIVER_H
