/*! \file phase_tracker.cpp
 *  \brief C++ file for the Phase Tracker block.
 *
 *  The phase tracker block is in charge of tracking and correcting
 *  phase rotation accross symbols in a frame using the 4 pilot subcarriers.
 *  It also removes the pilot and null subcarriers passing on only the data
 *  subcarriers after any necessary frequency corrections have been made.
 *
 */

#include <cstring>
#include <iostream>

#include "phase_tracker.h"

namespace fun
{

    /*! \brief The polarity of the pilot subcarriers beginning with the pilots of
     * the SIGNAL symbol being multiplied by POLARITY[0], then the next symbol
     * being multiplied by POLARITY[1] and so on.
     */
    const double POLARITY[127] = {
             1, 1, 1, 1,-1,-1,-1, 1,-1,-1,-1,-1, 1, 1,-1, 1,
            -1,-1, 1, 1,-1, 1, 1,-1, 1, 1, 1, 1, 1, 1,-1, 1,
             1, 1,-1, 1, 1,-1,-1, 1, 1, 1,-1, 1,-1,-1,-1, 1,
            -1, 1,-1,-1, 1,-1,-1, 1, 1, 1, 1, 1,-1,-1, 1, 1,
            -1,-1, 1,-1, 1,-1, 1, 1,-1,-1,-1, 1, 1,-1,-1,-1,
            -1, 1,-1,-1, 1,-1, 1, 1, 1, 1,-1, 1,-1, 1,-1, 1,
            -1,-1,-1,-1,-1, 1,-1, 1, 1,-1, 1,-1, 1, 1, 1,-1,
            -1, 1,-1,-1,-1, 1, 1, 1,-1,-1,-1,-1,-1,-1,-1
    };

    /*! \brief The index of each pilot in the 64 sample symbol and its
     * initial value before being multiplied by its corresponding polarity
     */
    static int PILOTS[4][2] =
    {
      { 11,  1 },
      { 25,  1 },
      { 39,  1 },
      { 53, -1 },
    };

    /*! \brief The indicies of the 48 data subcarriers in the 64 sample symbol */
    static int DATA_SUBCARRIERS[48] =
    {
       6,  7,  8,  9,  10,  /*11,*/ 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, /*25,*/ 26, 27, 28, 29, 30, 31,
      /*32,*/ 33, 34, 35, 36, 37, 38, /*39,*/ 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, /*53*/ 54, 55, 56, 57, 58
    };


    /*!
     * - Initializations:
     *   + #m_symbol_count -> 0
     */
    phase_tracker::phase_tracker() :
        block("phase_tracker"),
        m_symbol_count(0)

    {
    }

    /*!
     * This block uses the pilot symbols to estimate phase rotation of each symbol on a per symbol basis
     * The phase rotation of each pilot symbol is calculated then averaged together. The inverse of this
     * rotation is the applied to each symbol. This is a fair assumption since the pilot symbols are evenly
     * dispersed throughout the symbol.
     */
    void phase_tracker::work()
    {
        if(input_buffer.size() == 0) return;
        output_buffer.resize(input_buffer.size());

        for(int i = 0; i < input_buffer.size(); i++)
        {
            if(input_buffer[i].tag == START_OF_FRAME)
            {
                m_symbol_count = 0; // Reset the symbol count
            }

            // Calculate the phase error of this symbol based on the pilots
            std::complex<double> phase_error = std::complex<double>(0,0);
            for(int p = 0; p < 4; p++)
            {
                int pilot = PILOTS[p][1] * POLARITY[m_symbol_count % 127];
                std::complex<double> ref_pilot_sample = std::complex<double>(pilot, 0);
                std::complex<double> rec_pilot_sample = input_buffer[i].samples[PILOTS[p][0]];
                phase_error += rec_pilot_sample * std::conj(ref_pilot_sample) / 4.0;
            }

            double angle = std::arg(phase_error);

            // Apply the phase correction to the data samples
            for(int s = 0; s < 48; s++)
            {
                int index = DATA_SUBCARRIERS[s];
                output_buffer[i].samples[s] = input_buffer[i].samples[index] * std::complex<double>(std::cos(-angle), std::sin(-angle));
            }

            output_buffer[i].tag = input_buffer[i].tag;
            m_symbol_count++; //Keep track of the current symbol number in the frame
        }

    }

}




