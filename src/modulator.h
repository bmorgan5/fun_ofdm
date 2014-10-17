/*! \file modulator.h
 *  \brief Header file for Modulator class.
 *
 *  The modulator takes the input data in bits and converts it to
 *  complex doubles representing the digital modulation symbols and vice versa.
 *  Supported Modulations are:
 *  -BPSK
 *  -QPSK
 *  -16 QAM
 *  -64 QAM
 */


#ifndef MODULATOR_H
#define MODULATOR_H

#include <complex>

#include "rates.h"

namespace fun
{

    /*!
     * \brief The modulator class
     *
     *  The modulator takes the input data in bits and converts it to
     *  complex doubles representing the digital modulation symbols and vice versa.
     *  Supported Modulations are:
     *  -BPSK
     *  -QPSK
     *  -16 QAM
     *  -64 QAM
     */
    class modulator
    {
    public:

        /*!
         * \brief Modulates the data.
         * \param data Vector of data in bytes to be modulated.
         * \param rate PHY transmission rate from which the type of modulation is extracted.
         * \return Vector of modulated data as complex doubles.
         */
        static std::vector<std::complex<double> > modulate(std::vector<unsigned char> data, Rate rate);

        /*!
         * \brief Demodulates the data.
         * \param data Vector of data to be demodulated in complex doubles.
         * \param rate PHY transmission frate from which the type of modulation is extracted.
         * \return Vector of demodulated data in bytes.
         */
        static std::vector<unsigned char> demodulate(std::vector<std::complex<double> > data, Rate rate);
    };
}


#endif // MODULATOR_H
