/*! \file symbol_mapper.h
 *  \brief Header file for symbol mapper class
 *
 *  The symbol mapper class takes the stream of modulated data and converts it
 *  into symbols by mapping the data, pilots, and nulls onto their respective
 *  subcarriers. Conversely it also extracts the data from received symbols.
 */

#ifndef SYMBOL_MAPPER_H
#define SYMBOL_MAPPER_H

#include <vector>
#include <complex>

namespace fun
{
    /*!
     * \brief The symbol_mapper class
     *
     *  The symbol mapper class takes the stream of modulated data and maps it
     *  into symbols by mapping the data, pilots, and nulls onto their respective
     *  subcarriers. Conversely it also extracts the data from received symbols.
     */
    class symbol_mapper
    {

    public:

        /*!
         * \brief Constructor for symbol mapper class.
         */
        symbol_mapper();

        /*!
         * \brief Maps the data, pilots, and nulls to their respective subcarriers
         * \param data_samples Vector of modulated data to be mapped into symbols
         * \return Vector of symbols with data, pilots, and nulls
         */
        std::vector<std::complex<double> > map(std::vector<std::complex<double> > data_samples);

        /*!
         * \brief Extracts the data from the symbols throwing out the nulls and pilots
         * \param samples Vector of symbols to extract data from
         * \return Vector of data samples
         */
        std::vector<std::complex<double> > demap(std::vector<std::complex<double> > samples);

        /*!
         * \brief Gets the current active map of data, pilots, and nulls.
         * \return The current active map.
         */
        std::vector<unsigned char> get_active_map();

    private:

        static const std::vector<unsigned char> m_active_map; //!< The current map of data, pilots, and nulls.

        static const double POLARITY[127]; //!< The Pilot Polarity Sequence

        static const std::complex<double> PILOTS[4]; //!< The 4 Pilot symbols

        int m_data_subcarrier_count; //!< Number of data subcarriers.

        int m_pilot_count; //!< Number of pilot subcarriers.

    };
}


#endif // SYMBOL_MAPPER_H
