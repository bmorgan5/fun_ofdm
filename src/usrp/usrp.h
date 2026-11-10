/*! \file usrp.h
 *  \brief Sends and receives baseband samples with a USRP, through UHD.
 *
 *  This is the only code that talks to the radio. The PHY works on complex
 *  baseband samples, and the usrp class moves them to and from the device:
 *
 *  ~~~
 *  transmitter: frame_builder → send_burst_sync() → USRP → air
 *  receiver:    air → USRP → get_samples() → receiver_chain
 *  ~~~
 *
 *  Samples are std::complex<double> on the host (UHD's fc64 format), with
 *  ±1.0 as the converters' full scale.
 *
 *  The constructor opens the device and sets up both directions from one
 *  usrp_params: the same center frequency and sample rate, and each
 *  direction's gain. It then starts the RX stream, which runs until the object
 *  is destroyed. From then on the caller has to call get_samples() often
 *  enough to keep up with the sample rate, or the device overflows and drops
 *  samples.
 *
 *  Each transmit call sends one burst, marked as both the start and the end
 *  of a burst, so the transmitter is only on while it sends a frame.
 *
 *  \bug The constructor starts the RX stream even when the object is only used
 *  to transmit (by transmitter). Nothing reads it, so the device streams
 *  samples for as long as the object exists, only for them to be dropped.
 *
 *  \bug get_samples() ignores how many samples recv() returned, and its error
 *  code. After an overflow or a timeout, the end of the buffer still holds the
 *  previous batch, which the receiver chain then processes again.
 *
 *  \bug send_burst_sync() is meant to stop waiting on an underflow as well as
 *  on the end-of-burst acknowledgement, but its underflow check needs one
 *  event to be both, so it never fires.
 *
 *  \bug send_burst() doesn't scale the samples by usrp_params::tx_amp, as
 *  send_burst_sync() does.
 */

#ifndef USRP_H
#define USRP_H

#include <semaphore.h>

#include <cstdint>
#include <optional>

#include <uhd/convert.hpp>
#include <uhd/stream.hpp>
#include <uhd/usrp/multi_usrp.hpp>

#include "usrp_params.h"

namespace fun {
/*!
 * \brief What UHD said about a batch of received samples.
 */
struct rx_metadata {
  //! The device dropped samples before this batch, because they weren't read
  //! fast enough
  bool overflow = false;

  //! When the batch's first sample was taken, in nanoseconds on the device's
  //! clock, if UHD gave the time
  std::optional<std::int64_t> device_time;
};

/*!
 * \brief Sends and receives baseband samples with one USRP.
 */
class usrp {
 public:
  /*!
   * \brief Opens the device, sets it up from \p params, and starts the RX
   * stream.
   * \param params The device, frequency, sample rate, gains and transmit
   * amplitude. They stay fixed for the lifetime of the object.
   * \throws std::out_of_range if either gain is outside the device's range.
   * UHD would otherwise clip it without saying so.
   * \throws uhd::exception if UHD can't find or set up the device.
   */
  usrp(usrp_params params);

  /*!
   * \brief Sends one burst, and blocks until the device reports that it has
   * finished.
   * \param samples The burst's baseband samples. They're scaled by
   * usrp_params::tx_amp first.
   *
   *  Blocks until the device acknowledges the end of the burst, or until a
   *  second passes without any message from it.
   */
  void send_burst_sync(std::vector<std::complex<double>> samples);

  /*!
   * \brief Sends one burst, without waiting for it to go out.
   * \param samples The burst's baseband samples, sent unscaled.
   *
   *  Returns once UHD has taken the samples, which can be before the device
   *  has transmitted them. Nothing calls this today; transmitter uses
   *  send_burst_sync().
   */
  void send_burst(std::vector<std::complex<double>> samples);

  /*!
   * \brief Receives the next samples from the RX stream.
   * \param num_samples How many samples to receive.
   * \param buffer Where to put them: its first \p num_samples elements. It
   * must already hold at least that many.
   *
   *  Blocks until the samples arrive, or until UHD's receive timeout (0.1 s)
   *  runs out.
   *
   * \return Whether the device overflowed, and the batch's time.
   */
  rx_metadata get_samples(int num_samples,
                          std::vector<std::complex<double>>& buffer);

 private:
  usrp_params m_params;  //!< The parameters it was created with

  uhd::usrp::multi_usrp::sptr m_usrp;    //!< multi_usrp (main USRP handle)
  uhd::device::sptr m_device;            //!< device (receives async messages)
  uhd::rx_streamer::sptr m_rx_streamer;  //!< RX (input) streamer
  uhd::tx_streamer::sptr m_tx_streamer;  //!< TX (output) streamer

  sem_t m_tx_sem;  //!< Lets one send_burst() call send at a time
};

}  // namespace fun

#endif  // USRP_H
