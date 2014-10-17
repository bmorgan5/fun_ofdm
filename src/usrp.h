/*! \file usrp.h
 *  \brief Header file for the usrp class and the usrp_params struct.
 *
 *  The usrp class is a wrapper class for the UHD API. It provides a simple interface for transmitting
 *  and receiving samples to/from the USRP.
 *
 *  The usrp_params class is a container for holding the necessary parameters for the usrp
 *  such as center frequency, sample rate, tx/rx gain, etc..
 */

#ifndef USRP_H
#define USRP_H

#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/convert.hpp>
#include <uhd/stream.hpp>
#include <semaphore.h>

namespace fun
{
    /*!
     * \brief The usrp_params struct which holds parameters for the USRP object such as
     *  center frequency, sample rate, tx/rx gain, etc..
     */
    struct usrp_params
    {
        double freq;                //!< Center Frequency
        double rate;                //!< Sample Rate
        double tx_gain;             //!< Transmit Gain (0-35 for USRP N210)
        double rx_gain;             //!< Receive Gain  (0-35 for USRP N210)
        double tx_amp;              //!< Transmit Amplitude - scales all tx samples before sending to USRP
        std::string device_addr;    //!< IP Address of USRP as a string - i.e. "192.168.10.2" or "" to find automatically

        /*!
         * \brief Constructor for usrp_params. Simply initializes member fields to be looked up later.
         * \param freq -> #freq
         * \param rate -> #rate
         * \param tx_gain -> #tx_gain
         * \param rx_gain -> #rx_gain
         * \param tx_amp -> #tx_amp
         * \param device_addr -> #device_addr
         */
        usrp_params(double freq = 5.72e9, double rate = 5e6, double tx_gain=20, double rx_gain=20, double tx_amp=1.0, std::string device_addr="") :
            freq(freq),
            rate(rate),
            tx_gain(tx_gain),
            rx_gain(rx_gain),
            tx_amp(tx_amp),
            device_addr(device_addr)
        {
        }
    };

    /*!
     * \brief A simple class used to easily interface with a USRP.
     *
     *  The usrp class is a wrapper class for the UHD API. It provides a simple interface for transmitting
     *  and receiving samples to/from the USRP.
     */
    class usrp
    {
    public:

        /*!
         * \brief Constructor for usrp class.
         * \param params the parameters for the intance of this class. These parameters will remain
         *  fixed for the lifetime of the USRP object.
         */
        usrp(usrp_params params);


        // Send a burst of samples, and block until the burst has finished
        /*!
         * \brief Sends a burst of samples and block until the burst has finished.
         * \param samples A vector of complex doubles representing the base band time domain signal
         *  to be up-converted and transmitted by the USRP.
         */
        void send_burst_sync(std::vector<std::complex<double> > samples);

        /*!
         * \brief Sends a burst of samples but does not block until the burst has finished.
         * \param samples A vector of complex doubles representing the base band time domain signal
         *  to be up-converted and transmitted by the USRP.
         */
        void send_burst(std::vector<std::complex<double> > samples);

        // Get some samples from the USRP
        /*!
         * \brief Gets num_samples samples and places them in the first num_samples of buffer.
         * \param num_samples The number of samples to retrieve from USRP.
         * \param buffer The buffer to place the retrieved samples in.
         */
        void get_samples(int num_samples, std::vector<std::complex<double> > & buffer);


    private:


        usrp_params m_params; //!< Container for the parameters for this instance of the USRP class.

        boost::shared_ptr<uhd::usrp::multi_usrp> m_usrp; //!< multi_usrp (main USRP handle)
        boost::shared_ptr<uhd::device> m_device;         //!< device (receives async messages)
        uhd::rx_streamer::sptr m_rx_streamer;            //!< TX (output) streamer
        uhd::tx_streamer::sptr m_tx_streamer;            //!<  RX (input) streamer

        sem_t m_tx_sem;                                  //!< Sempahore used to block for #send_burst_sync
    };

}


#endif // USRP_H
