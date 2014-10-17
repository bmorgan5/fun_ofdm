/*! \file timing_sync.h
 *  \brief Hader file for the Timing Sync block.
 *
 *  The timing sync block is in charge of using the two LTS symbols to align the received frame in time.
 *  It also uses the two LTS symbols to perform an initial frequency offset estimation and
 *  applying the necessary correction.
 */

#ifndef TIMING_SYNC_H
#define TIMING_SYNC_H

#define LTS_CORR_THRESHOLD 0.9
#define CARRYOVER_LENGTH 160
#define LTS_LENGTH 64

#include <complex>

#include "block.h"
#include "tagged_vector.h"

namespace fun
{
    /*!
     * \brief The timing_sync block.
     *
     * Inputs tagged samples from the frame_detector block.
     * Outputs tagged samples to the fft_symbols block.
     *
     * The timing sync block is in charge of using the two LTS symbols to align the received frame in time.
     * It also uses the two LTS symbols to perform an initial frequency offset estimation and
     * applying the necessary correction.
     */
    class timing_sync : public fun::block<tagged_sample, tagged_sample>
    {
    public:

        timing_sync(); //!< Constructor for timing_sync block.

        virtual void work(); //!< Signal processing happens here.

    private:

        double m_phase_offset; //!< The phase rotation from symbol to symbol

        double m_phase_acc; //!< The total phase rotation for the current symbol

        /*!
         * \brief Vector for storing the last 160 samples from the input_buffer
         * and carrying them over to the next call to #work()
         */
        std::vector<tagged_sample> m_carryover;
    };
}

#endif // TIMING_SYNC_H
