/*! \file transmitter.h
 *  \brief Header file for transmitter class.
 *
 *  The transmitter class is the public interface for the fun_ofdm transmit chain.
 *  This is the easiest way to start transmitting 802.11a OFDM frames out of the box.
 */


#ifndef TRANSMITTER_H
#define TRANSMITTER_H

#include <vector>
#include "usrp.h"
#include "rates.h"
#include "frame_builder.h"

namespace fun {

    /*!
     * \brief The transmitter class is the public interface for the fun_ofdm transmit chain.
     *  This is the easiest way to start transmitting 802.11a OFDM frames out of the box.
     *
     *  Usage: Create a transmitter object with the desired USRP parameters (center frequency,
     *  sample rate, transmitter gain, and amplitude). Then to send a packet simply call
     *  the transmitter::send_frame() function passing it the desired packet to be transmitted
     *  and the desired Physical Layer rate (PHY Rate) to transmit it.
     */
    class transmitter
    {
    public:

        /*!
         * \brief Constructor for the transmitter with raw parameters
         * \param freq [Optional] Center frequency
         * \param samp_rate [Optional] Sample rate
         * \param tx_gain [Optional] Transmit Gain
         * \param tx_amp [Optional] Transmit Amplitude
         * \param device_addr [Optional] IP address of USRP device
         *
         *  Defaults to:
         *  - center_freq -> 5.72e9 (5.72 GHz)
         *  - sample_rate -> 5e6 (5 MHz)
         *  - tx_gain -> 20
         *  - device_addr -> "" (empty string will default to letting the UHD api
         *    automatically find an available USRP)
         *  - *Note:
         *     + rx_gain -> 20 even though it is irrelevant for the transmitter
         */
        transmitter(double freq = 5.72e9, double samp_rate = 5e6, double tx_gain = 20, double tx_amp=1.0, std::string device_addr="");

        /*!
         * \brief Constructor for the transmitter that uses the usrp_params struct
         * \param params [Optional] The usrp parameters you want to use for this transmitter
         *
         *  Defaults to:
         *  - center freq -> 5.72e9 (5.72 GHz)
         *  - sample rate -> 5e6 (5 MHz)
         *  - tx gain -> 20
         *  - rx gain -> 20 (although this is irrelevant for the transmitter)
         *  - device ip address -> "" (empty string will default to letting the UHD api
         *    automatically find an available USRP)
         */
        transmitter(usrp_params params = usrp_params());

        /*!
         * \brief Send a single PHY frame at the given PHY Rate
         * \param payload The data to be transmitted (i.e. the MPDU)
         * \param phy_rate [Optional] The PHY data rate to transmit at - defaults to 1/2 BPSK
         *
         *  This function uses the usrp::send_burst_sync() function which means that this function
         *  blocks until the packet is done transmitting.
         */
        void send_frame(std::vector<unsigned char> payload, Rate phy_rate = RATE_1_2_BPSK);

    private:

        usrp m_usrp; //!< The usrp object used to send the generated frames over the air

        frame_builder m_frame_builder; //!< The frame builder object used to generate the frames

    };

}



#endif // TRANSMITTER_H
