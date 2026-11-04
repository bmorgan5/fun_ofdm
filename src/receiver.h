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
#include <stop_token>
#include <vector>

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
   * \brief Opens the USRP, which starts its RX stream, and starts the receiver
   * chain's threads.
   * \param callback Called with the payloads decoded from each batch of
   * samples, which passed their CRC-32. It's called after every batch, with an
   * empty vector if none were decoded.
   * \param params The USRP's parameters. Defaults to the defaults in
   * #usrp_params. The transmit gain and amplitude aren't used.
   * \throws Whatever usrp::usrp() throws, if the device can't be opened or a
   * gain is out of range.
   *
   * Call run() soon after: the RX stream is already running, and the USRP
   * overflows if its samples aren't read.
   */
  receiver(void (*callback)(std::vector<std::vector<std::uint8_t>> packets),
           usrp_params params = usrp_params());

  /*!
   * \brief Receives frames until a stop is requested.
   * \param stop Stop token for ending the receiver. It is checked between
   * batches of #NUM_RX_SAMPLES samples, so run() returns within one batch of
   * the request.
   */
  void run(std::stop_token stop);

 private:
  //! Called with the payloads decoded from each batch of samples
  void (*m_callback)(std::vector<std::vector<std::uint8_t>> packets);

  usrp m_usrp;  //!< Reads the samples from the air

  receiver_chain m_rec_chain;  //!< Detects and decodes frames in the samples

  //! The latest batch of samples read from the USRP
  std::vector<std::complex<double>> m_samples;
};

}  // namespace fun

#endif  // RECEIVER_H
