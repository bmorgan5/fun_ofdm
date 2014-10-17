/*! \file frame_detector.cpp
 *  \brief C++ file for the Frame Detector block.
 *
 * This block is in charge of detecting the beginning of a frame using the
 * short training sequence in the preamble.
 */

#include <cstring>
#include <iostream>

#include "frame_detector.h"

namespace fun
{
    /*!
     * - Initializations:
     *   + #m_power_acc      -> #STS_LENGTH (16 samples)
     *   + #m_corr_acc       -> #STS_LENGTH (16 samples)
     *   + #m_carryover      -> #STS_LENGTH (16 samples)
     *   + #m_plateau_length -> 0
     *   + #m_plateau_flag   -> false
     */
    frame_detector::frame_detector() :
        block("frame_detector"),
        m_power_acc(STS_LENGTH),
        m_corr_acc(STS_LENGTH),
        m_carryover(STS_LENGTH, 0),
        m_plateau_length(0),
        m_plateau_flag(false)
    {
    }

    /*!
     * This block uses auto-correlation to detect the short training sequence.
     * This autocorrelation is achieved through a moving window average
     * using the circular accumulators to keep track of the current
     * auto-correlation and input power of the input samples. The normalized
     * auto-correlation is then compared to a threshold to determine if
     * the current samples are part of the STS or not.
     */
    void frame_detector::work()
    {
        if(input_buffer.size() == 0) return;
        output_buffer.resize(input_buffer.size());

        // Step through the samples
        for(int x = 0; x < input_buffer.size(); x++)
        {
            output_buffer[x].tag = NONE;

            // Get the delayed samples
            std::complex<double> delayed;
            if(x < STS_LENGTH) delayed = m_carryover[x];
            else delayed = input_buffer[x-STS_LENGTH];

            // Update the correlation accumulators
            m_corr_acc.add(input_buffer[x] * std::conj(delayed));

            // Update the power accumulator
            m_power_acc.add(std::norm(input_buffer[x]));

            // Calculate the normalized correlations
            double corr = std::abs(m_corr_acc.sum) / m_power_acc.sum;

            if(corr > PLATEAU_THRESHOLD)
            {
                m_plateau_length++;
                if(m_plateau_length == STS_PLATEAU_LENGTH)
                {
                    output_buffer[x].tag = STS_START;
                    m_plateau_flag = true;
                }
            }
            else
            {
                if(m_plateau_flag)
                {
                    output_buffer[x].tag = STS_END;
                    m_plateau_flag = false;
                }
                m_plateau_length = 0;
            }

            // Pass through the sample
            output_buffer[x].sample = input_buffer[x];
        }

        // Carryover the last 16 output samples
        memcpy(&m_carryover[0],
               &input_buffer[input_buffer.size() - STS_LENGTH],
               STS_LENGTH * sizeof(std::complex<double>));
    }

}


