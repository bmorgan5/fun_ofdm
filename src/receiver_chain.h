/*! \file receiver_chain.h
 *  \brief Header file for Receiver Chain class
 *
 *  The Receiver Chain class is the main controller for the blocks that are
 *  used to receive and decode PHY layer frames. It holds the instances of each block
 *  and shifts the data through the receive chain as it is processed eventually returning
 *  the correctly received payloads (MPDUs) which can then be passed to the upper layers.
 */

#ifndef RECEIVER_CHAIN_H
#define RECEIVER_CHAIN_H

#include <thread>
#include <mutex>
#include <condition_variable>

#include "block.h"
#include "fft_symbols.h"
#include "channel_est.h"
#include "phase_tracker.h"
#include "frame_decoder.h"
#include "tagged_vector.h"
#include "frame_detector.h"
#include "timing_sync.h"

namespace fun
{

    /*! \brief The Receiver Chain class.
     *
     *  Inputs raw complex doubles representing the base-band digitized time domain signal.
     *
     *  Outputs vector of correctly received payloads (MPDUs) which are themselves vectors
     *  of unsigned chars.
     *
     *  The Receiver Chain class is the main controller for the blocks that are
     *  used to receive and decode PHY layer frames. It holds the instances of each block
     *  and shifts the data through the receive chain as it is processed eventually returning
     *  the correctly received payloads (MPDUs) which can then be passed to the upper layers.
     */
    class receiver_chain : public block<std::complex<double>, std::vector<unsigned char>>
    {
    public:
        /*!
         * \brief Constructor for receiver_chain
         */
        receiver_chain();

        virtual ~receiver_chain();

        virtual void work();

        /*!
         * \brief Processes the raw time domain samples.
         * \param samples A vector of received time-domain samples from the usrp block to pass to
         *  the receive chain for signal processing.
         * \return A vector of correctly received payloads where each payload is its own vector
         *  of unsigned chars.
         */
        std::vector<std::vector<unsigned char>> process_samples(std::vector<std::complex<double>> &samples);

    private:
        /**********
         * Blocks *
         **********/

        frame_detector * m_frame_detector;     //!< Detects start of frame using STS
        timing_sync    * m_timing_sync;        //!< Aligns frame in time using LTS & some freq correction
        fft_symbols    * m_fft_symbols;        //!< Forward FFT of symbols
        channel_est    * m_channel_est;        //!< Channel estimation and equalization in freq domain
        phase_tracker  * m_phase_tracker;      //!< Phase rotation tracking
        frame_decoder  * m_frame_decoder;      //!< Frame decoding

        /***********************************
         * Scheduler Variables and Methods *
         ***********************************/

        /*!
         * \brief Adds block to the receiver call chain
         * \param block A pointer to the block so that its work function can be called
         */
        void add_block(block_base * block);

        void wait_on_wake(size_t index);

        void post_to_wake(size_t index);

        void wait_on_done(size_t index);

        void post_to_done(size_t index);

        /*!
         * \brief Runs the block by calling its work function
         * \param index the block's index for referencing the correct semaphores for that block.
         * \param block A pointer to the block used as a handle to access its work() function.
         */
        void run_block(block_base * block);

        /*!
         * \brief Halts all the blocks
         */
        void halt_blocks();

        std::vector<block_base*> m_blocks;
        std::vector<std::thread> m_threads; //!< Vector of threads - one for each block

        std::mutex m_halt_mtx;
        bool m_halt;
      };

}

#endif // RECEIVER_CHAIN_H
