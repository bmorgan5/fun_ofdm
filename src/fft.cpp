/*! \file fft.cpp
 *  \brief C++ for the fft class.
 *
 *  This class is a wrapper class on the fftw3 library and contains functions for
 *  performing 64 point forward and inverse FFTs.
 */

#include <cstring>
#include <assert.h>

#include "fft.h"

namespace fun
{

    /*!
     * This map shifts the subcarriers so that instead of being thought of sequentially from 0-63 they can
     * be thought of as positive and negative frequencies which is the ordering that the FFTW3 library uses.
     */
    const int fft::fft_map[64] =
    {
      32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
    };

    /*!
     * -Initializations:
     *  + #m_fft_length -> 64 because we always deal with 64 point FFTs since there are 64 OFDM subcarriers
     */
    fft::fft(int fft_length) :
        m_fft_length(fft_length)
    {
        // Allocate the FFT buffers
        m_fftw_in_forward = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * m_fft_length);
        m_fftw_out_forward = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * m_fft_length);
        m_fftw_in_inverse = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * m_fft_length);
        m_fftw_out_inverse = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * m_fft_length);
        m_fftw_plan_forward = fftw_plan_dft_1d(m_fft_length, m_fftw_in_forward, m_fftw_out_forward, FFTW_FORWARD, FFTW_MEASURE);
        m_fftw_plan_inverse = fftw_plan_dft_1d(m_fft_length, m_fftw_in_inverse, m_fftw_out_inverse, FFTW_BACKWARD, FFTW_MEASURE);
    }


    /*!
     * This function performs a single in-place 64 point FFT on the 64 complex data samples.
     * The user must loop over time-domain signal and pass each 64 sample symbol to this function
     * individually.
     * This function handles the shifting from all positive (0-63) indexing to
     * positive & negative frequency indexing.
     */
    void fft::forward(std::complex<double> data[64])
    {
        memcpy(m_fftw_in_forward, &data[0], m_fft_length * sizeof(std::complex<double>));
        fftw_execute(m_fftw_plan_forward);

        for(int s = 0; s < 64; s++)
        {
            memcpy(&data[s], &m_fftw_out_forward[fft_map[s]], sizeof(std::complex<double>));
        }
    }

    /*!
     * This function loops over the input vector (which must be an integer multiple of 64)
     * and performs in-place 64 point IFFTs on each consecutive 64 sample chunk of the input vector.
     * This function handles the shifting from positive & negative frequency (-32 to 31) indexing to
     * all positive (0 to 63) indexing.
     * This function also scales the output by 1/64 to be consistent with the IFFT function.
     */
    void fft::inverse(std::vector<std::complex<double> > & data)
    {
        assert(data.size() % m_fft_length == 0);

        // Run the IFFT on each m_fft_length samples
        for(int x = 0; x < data.size(); x += m_fft_length)
        {
            if(m_fft_length == 64)
            {
                for(int s = 0; s < 64; s++)
                {
                    memcpy(&m_fftw_in_inverse[s], &data[x + fft_map[s]], sizeof(std::complex<double>));
                }
            }
            else
            {
                memcpy(&m_fftw_in_inverse[0], &data[x], m_fft_length * sizeof(std::complex<double>));
            }

            fftw_execute(m_fftw_plan_inverse);
            memcpy(&data[x], m_fftw_out_inverse, m_fft_length * sizeof(std::complex<double>));
        }

        // Scale by 1/fft_length
        for(int x = 0; x < data.size(); x++)
        {
            data[x] /= m_fft_length;
        }
    }
}


