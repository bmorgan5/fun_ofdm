/*! \file usrp_params.h
 *  \brief The settings a usrp is created with.
 *
 *  It doesn't depend on UHD, so code that only builds the parameters (like the
 *  command line options) doesn't need UHD.
 */

#ifndef USRP_PARAMS_H
#define USRP_PARAMS_H

#include <string>

namespace fun {
/*!
 * \brief The settings a usrp is created with. Both directions share the
 * frequency and the sample rate.
 */
struct usrp_params {
  double freq = 5.72e9;     //!< Center frequency in Hz, for both TX and RX
  double rate = 5e6;        //!< Sample rate in samples/s, which is also the
                            //!< channel bandwidth in Hz
  double tx_gain = 20;      //!< Transmit gain in dB. The valid range depends on
                            //!< the device.
  double rx_gain = 20;      //!< Receive gain in dB. The valid range depends on
                            //!< the device.
  double tx_amp = 1.0;      //!< Scales each sample before it's sent, for finer
                            //!< control of the output power than tx_gain
  std::string device_addr;  //!< UHD device address, e.g.
                            //!< "addr=192.168.10.2", or "" to use the first
                            //!< device found
};
}  // namespace fun

#endif  // USRP_PARAMS_H
