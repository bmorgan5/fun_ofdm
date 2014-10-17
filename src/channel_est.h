/*! \file channel_est.h
 *  \brief Header file for the Channel Estimate block.
 *
 *  The Channel Estimate block is in charge of estimating the current channel conditions
 *  using the two known LTS symbols and equalizing the channel affect by applying the inverse
 *  of the channel attenuation & phase rotation to each of the subcarriers.
 */

#ifndef channel_est_H
#define channel_est_H

#include <vector>
#include <complex>

#include "tagged_vector.h"
#include "block.h"

namespace fun
{

    /*!
     * \brief The channel_est block.
     *
     * Inputs tagged_vector<64> from the fft_symbols block.
     *
     * Outputs tagged_vector<64> to the phase_tracker block.
     *
     * The Channel Estimate block is in charge of estimating the current channel conditions
     * using the two known LTS symbols and equalizing the channel affect by applying the inverse
     * of the channel attenuation & phase rotation to each of the subcarriers.
     */
    class channel_est : public fun::block<tagged_vector<64>, tagged_vector<64> >
    {
    public:


        channel_est(); //!< Construct for Channel Estimate block.

        virtual void work(); //!< Signal Processing happens here.

    private:


        std::vector<std::complex<double> > m_chan_est; //!< Current channel estimate for each subcarrier.

        /*!
         * \brief Flag to indicate whether the current symbols are part of the LTS or not.
         *
         * - Usage
         *   + 0: Not in the LTS
         *   + 1: Current symbol is the first LTS symbol
         *   + 2: Current symbol is the second LTS symbol
         */
        int m_lts_flag;

        /*!
         * \brief Flag to indicate whether the current symbol is the first symbol in the frame,
         * or in other words the first symbol after the second LTS symbol.
         */
        bool m_frame_start;
    };
}



#endif // channel_est_H
