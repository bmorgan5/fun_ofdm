/*! \file usrp_params.h
 *  \brief Header file for the usrp_params struct.
 *
 *  The usrp_params struct is a container for holding the necessary parameters
 * for the usrp such as center frequency, sample rate, tx/rx gain, etc.. It
 * doesn't depend on UHD, so code that only builds the parameters (like the
 * command line options) doesn't need UHD.
 */

#ifndef USRP_PARAMS_H
#define USRP_PARAMS_H

#include <string>

namespace fun {
/*!
 * \brief The usrp_params struct which holds parameters for the USRP object such
 * as center frequency, sample rate, tx/rx gain, etc..
 */
struct usrp_params {
  double freq = 5.72e9;  //!< Center Frequency
  double rate = 5e6;     //!< Sample Rate
  double tx_gain = 20;   //!< Transmit Gain (0-35 for USRP N210)
  double rx_gain = 20;   //!< Receive Gain  (0-35 for USRP N210)
  double tx_amp = 1.0;   //!< Transmit Amplitude - scales all tx samples before
                         //!< sending to USRP
  std::string device_addr;  //!< IP Address of USRP as a string - i.e.
                            //!< "192.168.10.2" or "" to find automatically
};
}  // namespace fun

#endif  // USRP_PARAMS_H
