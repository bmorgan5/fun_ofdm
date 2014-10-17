/*! \file frame_detector.h
 *  \brief Header file for frame_detector block.
 *
 * This block is in charge of detecting the beginning of a frame using the
 * short training sequence in the preamble.
 */

#ifndef FRAME_DETECTOR_H
#define FRAME_DETECTOR_H

//Tweakable Parameters
#define PLATEAU_THRESHOLD 0.9
#define STS_PLATEAU_LENGTH 16

#define STS_LENGTH 16

#include <complex>

#include "block.h"
#include "tagged_vector.h"
#include "circular_accumulator.h"

namespace fun
{
    /*!
     * \brief The frame_detector block.
     *
     * Inputs complex doubles from USRP block.
     * Outputs tagged samples to timing sync block.
     *
     * This block is in charge of detecting the beginning of a frame using the
     * short training sequence in the preamble.
     */
    class frame_detector : public fun::block<std::complex<double>, tagged_sample>
    {
    public:

        frame_detector(); //!< Constructor for frame_detector block.

        virtual void work(); //!< Signal processing happens here.

    private:

        /*!
         * \brief Circular accumulator for calculating correlation.
         */
        circular_accumulator<std::complex<double> > m_corr_acc;

        /*!
         * \brief Circular accumulator for calculating correlation.
         */
        circular_accumulator<double> m_power_acc;

        /*!
         * \brief Counter for keeping track of STS plateau length.
         */
        int m_plateau_length;

        /*!
         * \brief Flag for signaling whether we are currently in a
         * plateau or not.
         */
        bool m_plateau_flag;

        /*!
         * \brief Vector for storing the last 16 samples from the input_buffer
         * and carrying them over to the next call to #work()
         */
        std::vector<std::complex<double> > m_carryover;
    };
}



#endif // FRAME_DETECTOR_H
