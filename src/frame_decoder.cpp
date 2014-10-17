/*! \file frame_decoder.cpp
 *  \brief C++ file for the Frame Decoder block.
 *
 *  The Frame Decoder block is in charge of decoding the frame once the data
 *  subcarriers have been recovered from the received frame.
 */

#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <boost/crc.hpp>

#include "frame_decoder.h"
#include "qam.h"
#include "interleaver.h"
#include "viterbi.h"
#include "parity.h"
#include "rates.h"
#include "modulator.h"
#include "puncturer.h"
#include "interleaver.h"
#include "ppdu.h"

namespace fun
{
    /*!
     * - Initializations:
     *   + #m_current_frame -> Reset to a frame of 0 length with RATE_1_2_BPSK
     */
    frame_decoder::frame_decoder() :
        block("frame_decoder"),
        m_current_frame(FrameData(RateParams(RATE_1_2_BPSK)))
    {
        m_current_frame.Reset(RateParams(RATE_1_2_BPSK), 0, 0);
    }

    /*!
     * When a start of frame is detected this block first attempts to decode the ppdu header.
     * If that is successful as determined by a simple parity check on the header bits it
     * then tries to decode the payload of the frame using the parameters it gathered from
     * header.  If that is successful as deteremined by an IEEE CRC-32 check, the decoded payload
     * is passed to the output_buffer to be returned to the receive chain so that it can be passed
     * up to the MAC layer.
     */
    void frame_decoder::work()
    {
        if(input_buffer.size() == 0) return;
        output_buffer.resize(0);

        // Step through each 48 sample symbol
        for(int x = 0; x < input_buffer.size(); x++)
        {
            // Copy over available symbols
            if(m_current_frame.samples_copied < m_current_frame.sample_count)
            {
                memcpy(&m_current_frame.samples[m_current_frame.samples_copied], &input_buffer[x].samples[0], 48 * sizeof(std::complex<double>));
                m_current_frame.samples_copied += 48;
            }

            // Decode the frame if possible
            if(m_current_frame.samples_copied >= m_current_frame.sample_count && m_current_frame.sample_count != 0)
            {
                ppdu frame = ppdu(m_current_frame.rate_params.rate, m_current_frame.length);
                if(frame.decode_data(m_current_frame.samples))
                {
                    output_buffer.push_back(frame.get_payload());
                }
                m_current_frame.sample_count = 0;
            }

            // Look for a start of frame
            if(input_buffer[x].tag == START_OF_FRAME)
            {
                // Attempt to decode the header
                ppdu h = ppdu();
                std::vector<std::complex<double> > header_samples(48);
                memcpy(header_samples.data(), input_buffer[x].samples, 48 * sizeof(std::complex<double>));
                if(!h.decode_header(header_samples)) continue;

                // Calculate the frame sample count
                int length = h.get_length();
                RateParams rate_params = RateParams(h.get_rate());
                int frame_sample_count = h.get_num_symbols() * 48;

                // Start a new frame
                m_current_frame.Reset(rate_params, frame_sample_count, length);
                m_current_frame.samples.resize(h.get_num_symbols() * 48);
                continue;
            }
        }
    }
}


