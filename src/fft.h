/*! \file fft.h
 *  \brief Header file for the fft class.
 *
 *  This class is a wrapper class on the fftw3 library and contains functions for
 *  performing 64 point forward and inverse FFTs.
 */

#ifndef FFT_H
#define FFT_H

#include <complex>
#include <fftw3.h>
#include <vector>

namespace fun
{
    /*!
     * \brief The fft class
     *
     * This class contains is a wrapper for the fftw3 library and contains the functions
     * and necessary parameters for performing the IFFTs and FFTs in the transmit and receive
     * chains respectively.
     */
    class fft
    {
    public:


        /*!
         * \brief Constructor for fft object
         * \param fft_length length of FFT - i.e. 64 point FFT
         */
        fft(int fft_length);

        /*!
         * \brief In place 64 point forward FFT.
         * \param data Array of 64 complex samples in time domain to be
         *  converted to frequency domain.
         */
        void forward(std::complex<double> data[64]);

        /*!
         * \brief In place inverse FFT of input data.
         * \param data Vector of complex doubles in frequency domain to be
         *  converted to time domain. The length of the data vector must
         *  be an integer multiple of #m_fft_length.
         */
        void inverse(std::vector<std::complex<double> > & data);

    private:

        /*!
         * \brief Mapping to/from FFT order.
         */
        static const int fft_map[64];

        /*!
         * \brief Length of FFT. In 802.11a it is always a 64 Point FFT since
         *  There are 64 subcarriers.
         */
        int m_fft_length;        

        /*!
         * \brief Forward input buffer for use by fftw3 library.
         */
        fftw_complex * m_fftw_in_forward;

        /*!
         * \brief Forward output buffer for use by fftw3 library.
         */
        fftw_complex * m_fftw_out_forward;

        /*!
         * \brief Inverse input buffer for use by fftw3 library.
         */
        fftw_complex * m_fftw_in_inverse;

        /*!
         * \brief Inverse output buffer for use by fftw3 library.
         */
        fftw_complex * m_fftw_out_inverse;

        /*!
         * \brief Forward FFT plan for use by fftw3 library.
         */
        fftw_plan m_fftw_plan_forward;

        /*!
         * \brief Inverse FFT plan for use by fftw3 library.
         */
        fftw_plan m_fftw_plan_inverse;
    };
}


#endif // FFT_H
