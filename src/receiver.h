/*! \file receiver.h
 *  \brief Header file for the receiver class.
 *
 *  The receiver class is the public interface for the fun_ofdm receiver.
 *  This is the easiest way to start receiving 802.11a OFDM frames out of the box.
 */

#ifndef RECEIVER_H
#define RECEIVER_H

#include "block.h"
#include "usrp.h"
#include "receiver_chain.h"

#include <vector>
// #include <semaphore.h>

#define NUM_RX_SAMPLES 8192

namespace fun
{

    /*!
     * \brief The receiver class is the public interface for the fun_ofdm receiver.
     *  This is the easiest way to start receiving 802.11a OFDM frames out of the box.
     *
     *  Usage: To receive packets simply create a receiver object and pass it a callback
     *  function that takes a std::vector<std::vector<unsigned char> > as an input parameter.
     *  The receiver object then automatically creates a separate thread that pulls samples
     *  from the USRP and processes them with the receive chain. The received packets (if any)
     *  are then passed into the callback function where the user is able to process them further.
     *
     *  If at any time the user wishes to pause the receiver (i.e. so that the user can transmit
     *  some packets) the user simply needs to call the receiver::pause() function on the receiver
     *  object. Similarly, the receiver::resume() function can then be used to begin receiving again
     *  after a pause.
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
        receiver(void(*callback)(std::vector<std::vector<unsigned char>> &packets), double freq = 5.72e9, double samp_rate = 5e6, double rx_gain = 20, std::string device_addr = "");

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
        receiver(void(*callback)(std::vector<std::vector<unsigned char>> &packets), usrp_params params = usrp_params());

        // TODO: deprecated
        void resume();

        /*!
         * \brief Pauses the receiver thread.
         */
        void pause();

        /*!
         * \brief Starts the receiver thread after it has been paused (can be used after pause).
         */
        void start();

        /*!
         * \brief Halt the entire receiver
         */
        void halt();

    private:

        void receiver_chain_loop(); //!< Infinite while loop where samples are received from USRP and processed by the receiver_chain

        void rx_chain_loop();
        void run_block(block_base * block);


        /*!
         * \brief Thread safe way to check if the reciever has been halted
         *  This function can block until it can read m_halt safely
         */
        bool check_halt();

        void (*m_callback)(std::vector<std::vector<unsigned char>> &packets); //!< Callback function pointer

        std::vector<std::vector<unsigned char>> m_packets;
        std::thread m_rec_thread;
        std::vector<std::complex<double>> m_samples;

        usrp *m_usrp; //!< The usrp object used to receiver frames over the air
        std::thread m_usrp_thread;

        receiver_chain* m_rec_chain; //!< The receiver chain object used to detect & decode incoming frames
        std::thread m_rec_chain_thread;

        std::thread m_rec_chain_loop_thread;
        std::thread m_callback_thread;



        // std::thread m_recv_chain_loop_thread;

        std::mutex m_halt_mtx;
        bool m_halt;

        // sem_t m_pause; //!< Semaphore used to pause the receiver thread

    };


}

#endif // RECEIVER_H
