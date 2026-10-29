/*! \file usrp.h
 *  \brief Header file for the usrp class.
 *
 *  The usrp class is a wrapper class for the UHD API. It provides a simple
 * interface for transmitting and receiving samples to/from the USRP.
 */

#ifndef USRP_H
#define USRP_H

#include <semaphore.h>

#include <uhd/convert.hpp>
#include <uhd/stream.hpp>
#include <uhd/usrp/multi_usrp.hpp>

#include "usrp_params.h"

namespace fun {
/*!
 * \brief A simple class used to easily interface with a USRP.
 *
 *  The usrp class is a wrapper class for the UHD API. It provides a simple
 * interface for transmitting and receiving samples to/from the USRP.
 */
class usrp {
 public:
  /*!
   * \brief Constructor for usrp class.
   * \param params the parameters for the intance of this class. These
   * parameters will remain fixed for the lifetime of the USRP object.
   */
  usrp(usrp_params params);

  // Send a burst of samples, and block until the burst has finished
  /*!
   * \brief Sends a burst of samples and block until the burst has finished.
   * \param samples A vector of complex doubles representing the base band time
   * domain signal to be up-converted and transmitted by the USRP.
   */
  void send_burst_sync(std::vector<std::complex<double>> samples);

  /*!
   * \brief Sends a burst of samples but does not block until the burst has
   * finished.
   * \param samples A vector of complex doubles representing the base band time
   * domain signal to be up-converted and transmitted by the USRP.
   */
  void send_burst(std::vector<std::complex<double>> samples);

  // Get some samples from the USRP
  /*!
   * \brief Gets num_samples samples and places them in the first num_samples of
   * buffer.
   * \param num_samples The number of samples to retrieve from USRP.
   * \param buffer The buffer to place the retrieved samples in.
   */
  void get_samples(int num_samples, std::vector<std::complex<double>>& buffer);

 private:
  usrp_params m_params;  //!< Container for the parameters for this instance of
                         //!< the USRP class.

  uhd::usrp::multi_usrp::sptr m_usrp;    //!< multi_usrp (main USRP handle)
  uhd::device::sptr m_device;            //!< device (receives async messages)
  uhd::rx_streamer::sptr m_rx_streamer;  //!< TX (output) streamer
  uhd::tx_streamer::sptr m_tx_streamer;  //!<  RX (input) streamer

  sem_t m_tx_sem;  //!< Sempahore used to block for #send_burst_sync
};

}  // namespace fun

#endif  // USRP_H
