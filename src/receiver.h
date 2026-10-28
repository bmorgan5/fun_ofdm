/*! \file receiver.h
 *  \brief Header file for the receiver class.
 *
 *  The receiver class is the public interface for the fun_ofdm receiver.
 *  This is the easiest way to start receiving 802.11a OFDM frames out of the box.
 */

#ifndef RECEIVER_H
#define RECEIVER_H

#include <stop_token>
#include <vector>
#include "receiver_chain.h"
#include "usrp.h"

#define NUM_RX_SAMPLES 4096

namespace fun
{

    /*!
     * \brief The receiver class is the public interface for the fun_ofdm receiver.
     *  This is the easiest way to start receiving 802.11a OFDM frames out of the box.
     *
     *  Usage: To receive packets simply create a receiver object, passing it a callback
     *  function that takes a std::vector<std::vector<unsigned char> > as an input parameter,
     *  and then call receiver::run(). run() pulls samples from the USRP and processes them with
     *  the receive chain on the calling thread. The received packets (if any) are passed into
     *  the callback function where the user is able to process them further. run() returns
     *  once a stop is requested on the std::stop_token passed to it.
     */
    class receiver
    {
    public:

        /*!
         * \brief Constructor for the receiver with raw parameters
         * \param callback Function pointer to the callback function where received packets are passed
         * \param freq [Optional] Center frequency
         * \param samp_rate [Optional] Sample Rate
         * \param rx_gain [Optional] Receive Gain
         * \param device_addr [Optional] IP address of USRP device
         *
         *  Defaults to:
         *  - center freq -> 5.72e9 (5.72 GHz)
         *  - sample rate -> 5e6 (5 MHz)
         *  - rx gain -> 20
         *  - device ip address -> "" (empty string will default to letting the UHD api
         *    automatically find an available USRP)
         *  - *Note:
         *    + tx_gain -> 20 even though it is irrelevant for the receiver
         *    + amp -> 1.0 even though it is irrelevant for the receiver
         */
        receiver(void(*callback)(std::vector<std::vector<unsigned char> > packets), double freq = 5.72e9, double samp_rate = 5e6, double rx_gain = 20, std::string device_addr = "");

        /*!
         * \brief Constructor for the receiver that uses the usrp_params struct
         * \param callback Function pointer to the callback function where received packets are passed
         * \param params [Optional] The usrp parameters you want to use for this receiver.
         *
         *  Defaults to:
         *  - center freq -> 5.72e9 (5.72 GHz)
         *  - sample rate -> 5e6 (5 MHz)
         *  - tx gain -> 20
         *  - rx gain -> 20 (although this is irrelevant for the transmitter)
         *  - device ip address -> "" (empty string will default to letting the UHD api
         *    automatically find an available USRP)
         */
        receiver(void(*callback)(std::vector<std::vector<unsigned char> > packets), usrp_params params = usrp_params());

        /*!
         * \brief Receives frames until a stop is requested.
         * \param stop Stop token for ending the receiver. It is checked between batches of
         *  #NUM_RX_SAMPLES samples, so run() returns within one batch of the request.
         */
        void run(std::stop_token stop);

    private:

        void (*m_callback)(std::vector<std::vector<unsigned char> > packets); //!< Callback function pointer

        usrp m_usrp; //!< The usrp object used to receiver frames over the air

        receiver_chain m_rec_chain; //!< The receiver chain object used to detect & decode incoming frames

        std::vector<std::complex<double> > m_samples; //!< Vector to hold the raw samples received from the USRP and passed into the receiver_chain
    };


}

#endif // RECEIVER_H
