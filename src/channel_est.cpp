/*! \file channel_est.cpp
 *  \brief C++ file for the Channel Estimate block.
 *
 *  The Channel Estimate block is in charge of estimating the current channel conditions
 *  using the two known LTS symbols and equalizing the channel affect by applying the inverse
 *  of the channel attenuation & phase rotation to each of the subcarriers.
 */

#include <cstring>

#include "channel_est.h"
#include "preamble.h"

namespace fun
{
    /*!
     * - Initializations:
     *   + #m_chan_est -> 64 complex doubles each initialized to (1+0j)
     *   + #m_lts_flag -> 0 or in other words not in the LTS
     *   + #m_frame_start -> false
     */
    channel_est::channel_est() :
        block("channel_est"),        
        m_chan_est(64, std::complex<double>(1, 0)),
        m_lts_flag(0),
        m_frame_start(false)
    {
    }

    /*!
     * This block constantly looks for the LTS_START flag to indicate the first LTS symbol.
     * Once this symbol is found it then compares each sample in the two LTS symbols with the known
     * transmitted sample and calculates the inverse channel effect. It then applies this
     * channel correction to the rest of the symbol.
     */
    void channel_est::work(){

        if(input_buffer.size() == 0) return;
        output_buffer.resize(0);

        for(int i = 0; i < input_buffer.size(); i++)
        {
            // Start of LTS found
            if(input_buffer[i].tag == LTS_START)
            {
                m_lts_flag = 1;
                for(int j = 0; j < 64; j++) m_chan_est[j] = std::complex<double>(0.0,0.0);
            }

            if(m_lts_flag > 0) // This is a LTS symbol
            {
                // Calculate channel correction
                for(int j = 0; j < 64; j++)
                {
                    std::complex<double> ref_lts_sample = LTS_FREQ_DOMAIN[j];
                    std::complex<double> rec_lts_sample = input_buffer[i].samples[j];
                    m_chan_est[j] += ref_lts_sample / rec_lts_sample / 2.0;
                }

                m_lts_flag++;
                if(m_lts_flag == 3) // No more LTS symbols
                {
                    m_lts_flag = 0;
                    m_frame_start = true; // Next symbol is the start of frame
                }
            }
            else
            {
                tagged_vector<64> symbol;
                if(m_frame_start)
                {
                    symbol.tag = START_OF_FRAME;
                    m_frame_start = false;
                }

                // Apply channel correction
                for(int j = 0; j < 64; j++)
                {
                    std::complex<double> out_sample = m_chan_est[j] * input_buffer[i].samples[j];
                    symbol.samples[j] = out_sample;
                }
                output_buffer.push_back(symbol);
            }
        }
    }

}


