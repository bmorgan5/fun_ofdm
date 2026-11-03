/*! \file receiver.h
 *  \brief Header file for the receiver class.
 *
 *  The receiver class is the public interface for the fun_ofdm receiver.
 *  This is the easiest way to start receiving 802.11a OFDM frames out of the
 * box.
 */

#ifndef RECEIVER_H
#define RECEIVER_H

#include <cstdint>
#include <stop_token>
#include <vector>

#include "src/phy/receiver_chain.h"
#include "src/usrp/usrp.h"

#define NUM_RX_SAMPLES 4096

namespace fun {

/*!
 * \brief The receiver class is the public interface for the fun_ofdm receiver.
 *  This is the easiest way to start receiving 802.11a OFDM frames out of the
 * box.
 *
 *  Usage: To receive packets simply create a receiver object, passing it a
 * callback function that takes a std::vector<std::vector<std::uint8_t> > as an
 * input parameter, and then call receiver::run(). run() pulls samples from the
 * USRP and processes them with the receive chain on the calling thread. The
 * received packets (if any) are passed into the callback function where the
 * user is able to process them further. run() returns once a stop is requested
 * on the std::stop_token passed to it.
 */
class receiver {
 public:
  /*!
   * \brief Constructor for the receiver
   * \param callback Function pointer to the callback function where received
   * packets are passed
   * \param params [Optional] The usrp parameters you want to use for this
   * receiver. Defaults to the defaults in #usrp_params. The tx gain and
   * amplitude are irrelevant for the receiver.
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
  void (*m_callback)(std::vector<std::vector<std::uint8_t>>
                         packets);  //!< Callback function pointer

  usrp m_usrp;  //!< The usrp object used to receiver frames over the air

  receiver_chain m_rec_chain;  //!< The receiver chain object used to detect &
                               //!< decode incoming frames

  std::vector<std::complex<double>>
      m_samples;  //!< Vector to hold the raw samples received from the USRP and
                  //!< passed into the receiver_chain
};

}  // namespace fun

#endif  // RECEIVER_H
