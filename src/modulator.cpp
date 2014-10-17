/*! \file modulator.cpp
 *  \brief C++ file for the Modulator class.
 *
 *  The modulator takes the input data in bits and converts it to
 *  complex doubles representing the digital modulation symbols and vice versa.
 *  Supported Modulations are:
 *  -BPSK
 *  -QPSK
 *  -16 QAM
 *  -64 QAM
 */

#include <cstring>

#include "modulator.h"
#include "qam.h"

namespace fun
{

    /*!
     *  Modulates the input data vector using one of the following modulations
     *  based on the given rate:
     *  -BPSK
     *  -QPSK
     *  -16 QAM
     *  -64 QAM
     */
    std::vector<std::complex<double> > modulator::modulate(std::vector<unsigned char> data, Rate rate)
    {
        // Modualate the data
        int modulated_sample_count = data.size();
        std::vector<double> data_mod_buffer;
        switch(rate)
        {
            // BPSK
            case RATE_1_2_BPSK: case RATE_2_3_BPSK: case RATE_3_4_BPSK:
            {
                QAM<1> bpsk(1.0);
                data_mod_buffer = std::vector<double>(modulated_sample_count * 2, 0);
                for(int x = 0; x < modulated_sample_count; x++)
                {
                    bpsk.encode((const char *)&data[x], &data_mod_buffer[x*2]);
                }

                break;
            }

            // QPSK
            case RATE_1_2_QPSK: case RATE_2_3_QPSK: case RATE_3_4_QPSK:
            {
                QAM<1> qpsk(0.5);
                modulated_sample_count /= 2;
                data_mod_buffer = std::vector<double>(modulated_sample_count * 2, 0);
                for(int x = 0; x < modulated_sample_count; x++)
                {
                    qpsk.encode((const char *)&data[x*2], &data_mod_buffer[x*2]);
                    qpsk.encode((const char *)&data[x*2+1], &data_mod_buffer[x*2+1]);
                }

                break;
            }

            // QAM16
            case RATE_1_2_QAM16: case RATE_2_3_QAM16: case RATE_3_4_QAM16:
            {
                QAM<2> qam16(0.5);
                modulated_sample_count /= 4;
                data_mod_buffer = std::vector<double>(modulated_sample_count * 2, 0);
                for(int x = 0; x < modulated_sample_count; x++)
                {
                    qam16.encode((const char *)&data[x*4], &data_mod_buffer[x*2]);
                    qam16.encode((const char *)&data[x*4+2], &data_mod_buffer[x*2+1]);
                }

                break;
            }

            // QAM64
            case RATE_2_3_QAM64: case RATE_3_4_QAM64:
            {
                QAM<3> qam64(0.5);
                modulated_sample_count /= 6;
                data_mod_buffer = std::vector<double>(modulated_sample_count * 2, 0);
                for(int x = 0; x < modulated_sample_count; x++)
                {
                    qam64.encode((const char *)&data[x*6], &data_mod_buffer[x*2]);
                    qam64.encode((const char *)&data[x*6+3], &data_mod_buffer[x*2+1]);
                }

                break;
            }
        }

        std::vector<std::complex<double> > modulated_data(data_mod_buffer.size() / 2);
        memcpy(&modulated_data[0], &data_mod_buffer[0], modulated_data.size() * sizeof(std::complex<double>));
        return modulated_data;
    }

    /*!
    *  Demodulates the input data vector using one of the following modulations
    *  based on the given rate:
    *  -BPSK
    *  -QPSK
    *  -16 QAM
    *  -64 QAM
    */
    std::vector<unsigned char> modulator::demodulate(std::vector<std::complex<double> > data, Rate rate)
    {
        RateParams rp = RateParams(rate);

        // Demodulate the data
        int coded_bit_count = data.size();
        std::vector<unsigned char>data_demodulated(coded_bit_count * rp.bpsc, 0);
        switch(rate)
        {
            // BPSK
            case RATE_1_2_BPSK: case RATE_2_3_BPSK: case RATE_3_4_BPSK:
            {
                QAM<1> bpsk(1.0);
                for(int s = 0; s < coded_bit_count; s++)
                    bpsk.decode(data[s].real(), &data_demodulated[s]);
                break;
            }

            // QPSK
            case RATE_1_2_QPSK: case RATE_2_3_QPSK: case RATE_3_4_QPSK:
            {
                QAM<1> qpsk(0.5);
                for(int s = 0; s < coded_bit_count; s++)
                {
                    qpsk.decode(data[s].real(), &data_demodulated[s*2]);
                    qpsk.decode(data[s].imag(), &data_demodulated[s*2+1]);
                }
                break;
            }

            // QAM16
            case RATE_1_2_QAM16: case RATE_2_3_QAM16: case RATE_3_4_QAM16:
            {
                QAM<2> qam16(0.5);
                for(int s = 0; s < coded_bit_count; s++)
                {
                    qam16.decode(data[s].real(), &data_demodulated[s*4]);
                    qam16.decode(data[s].imag(), &data_demodulated[s*4+2]);
                }
                break;
            }

            // QAM64
            case RATE_2_3_QAM64: case RATE_3_4_QAM64:
            {
                QAM<3> qam64(0.5);
                for(int s = 0; s < coded_bit_count; s++)
                {
                    qam64.decode(data[s].real(), &data_demodulated[s*6]);
                    qam64.decode(data[s].imag(), &data_demodulated[s*6+3]);
                }
                break;
            }
        }

        return data_demodulated;
    }
}

