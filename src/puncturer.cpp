/*! \file puncturer.cpp
 *  \brief C++ file for the puncture class.
 *
 *  The puncturer class punctures the convolutionally encoded data to
 *  effectively increase the coding rate.  On the receive side it
 *  depunctures the received punctured data by inserting 0's into the
 *  "puncture holes".
 */

#include <cmath>

#include "puncturer.h"

namespace fun
{

    /*!
     *  Punctures the convolutionally encoded data based on the desired PHY rate in rate_params.
     *  Suported Rates:
     *  - 1/2
     *  - 2/3
     *  - 3/4
     */
    std::vector<unsigned char> puncturer::puncture(std::vector<unsigned char> data, RateParams rate_params)
    {
        // Puncture the data
        int count, index;
        switch(rate_params.rate)
        {
            // Nothing to do
            case RATE_1_2_BPSK: case RATE_1_2_QPSK: case RATE_1_2_QAM16:
                return data;
                break;

            // Puncture from 1/2 to 3/4
            case RATE_3_4_BPSK: case RATE_3_4_QPSK: case RATE_3_4_QAM16: case RATE_3_4_QAM64:
            {
                count = round(data.size() * rate_params.rel_rate);
                std::vector<unsigned char> punc_buff_34(count);
                index = 0;
                for(int x = 0; x < data.size(); x += 6)
                {
                    punc_buff_34[index++] = data[x + 0];
                    punc_buff_34[index++] = data[x + 1];
                    punc_buff_34[index++] = data[x + 3];
                    punc_buff_34[index++] = data[x + 5];
                }
                return punc_buff_34;
            }
            break;

            // Puncture from 1/2 to 2/3
            case RATE_2_3_BPSK: case RATE_2_3_QPSK: case RATE_2_3_QAM16: case RATE_2_3_QAM64:
            {
                count = round(data.size() * rate_params.rel_rate);
                std::vector<unsigned char> punc_buff_23(count);
                index = 0;
                for(int x = 0; x < data.size(); x += 4)
                {
                    punc_buff_23[index++] = data[x + 0];
                    punc_buff_23[index++] = data[x + 2];
                    punc_buff_23[index++] = data[x + 3];
                }
                return punc_buff_23;
            }
            break;
        }
    }

    /*!
     *  Depunctures the punctured data by inserting 0's into the "puncture holes" based on the PHY
     *  rate in rate_params.
     *  Supported Rates:
     *  - 1/2
     *  - 2/3
     *  - 3/4
     */
    std::vector<unsigned char> puncturer::depuncture(std::vector<unsigned char> data, RateParams rate_params)
    {
        int sym_buffer_index = 0;
        int psc = 0;
        switch(rate_params.rate)
        {
            // Nothing to do
            case RATE_1_2_BPSK: case RATE_1_2_QPSK: case RATE_1_2_QAM16:
                return data;
                break;

            // De-puncture from 3/4 to 1/2 coding rate
            case RATE_3_4_BPSK: case RATE_3_4_QPSK: case RATE_3_4_QAM16: case RATE_3_4_QAM64:
            {
                psc = round(data.size() / rate_params.rel_rate);
                std::vector<unsigned char> sym_buffer34(psc);
                for(int x = 0; x < data.size(); x += 4)
                {
                  sym_buffer34[sym_buffer_index++] = data[x + 0];
                  sym_buffer34[sym_buffer_index++] = data[x + 1];
                  sym_buffer34[sym_buffer_index++] = 127;
                  sym_buffer34[sym_buffer_index++] = data[x + 2];
                  sym_buffer34[sym_buffer_index++] = 127;
                  sym_buffer34[sym_buffer_index++] = data[x + 3];
                }
                return sym_buffer34;
                break;
            }

            // De-ouncture from 2/3 to 1/2 coding rate
            case RATE_2_3_BPSK: case RATE_2_3_QPSK: case RATE_2_3_QAM16: case RATE_2_3_QAM64:
            {
                psc = round(data.size() / rate_params.rel_rate);
                std::vector<unsigned char> sym_buffer23(psc);
                for(int x = 0; x < data.size(); x += 3)
                {
                  sym_buffer23[sym_buffer_index++] = data[x + 0];
                  sym_buffer23[sym_buffer_index++] = 127;
                  sym_buffer23[sym_buffer_index++] = data[x + 1];
                  sym_buffer23[sym_buffer_index++] = data[x + 2];
                }
                return sym_buffer23;
                break;
            }
        }
    }

}

