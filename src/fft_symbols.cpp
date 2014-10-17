/*! \file fft_symbols.cpp
 *  \brief C++ file for the FFT Symbols block.
 *
 *  This block is in charge of chopping off the CP and performing a forward FFT on each
 *  symbol in the received chain.
 */

#include <fftw3.h>
#include <cstring>

#include "fft.h"
#include "fft_symbols.h"

namespace fun
{
    /*!
     * - Initializations:
     *   + #m_offset -> 0
     *   + #m_ffft -> Instance of 64 point forward fft class
     */
    fft_symbols::fft_symbols() :
        block("fft_symbols"),
        m_offset(0),
        m_ffft(64)
    {
    }

    /*!
     * This block removes the cyclic prefix and vectorizes the samples into 64 sample symbols
     * based on the tags marking the frame boundaries. It then performs a  64 point forward
     * fft on each symbol to convert it from time domain to frequency domain.
     */
    void fft_symbols::work()
    {
        if(input_buffer.size() == 0) return;
        output_buffer.resize(0);

        // Step through the input samples
        for(int x = 0; x < input_buffer.size(); x++)
        {
            // Check if this is the start of a new frame
            if(input_buffer[x].tag == LTS1)
            {
                // Push the current vector to the output buffer if
                // we've written any data to it
                if(m_offset > 15) output_buffer.push_back(m_current_vector);

                // Start a new vector
                m_current_vector.tag = LTS_START;
                m_offset = 16;
            }

            if(input_buffer[x].tag == LTS2)
            {
                m_offset = 16;
            }

            // Copy over samples past the cyclic prefix
            if(m_offset > 15)
            {
                m_current_vector.samples[m_offset - 16] = input_buffer[x].sample;
            }

            // Increment the offset and reset if we're at the end of the symbol
            m_offset++;
            if(m_offset == 80)
            {
                output_buffer.push_back(m_current_vector);
                m_current_vector.tag = NONE;
                m_offset = 0;
            }
        }

        // Perform forward FFT
        for(int x = 0; x < output_buffer.size(); x++)
        {
            m_ffft.forward(output_buffer[x].samples);

        }
    }
}


