/*! \file puncturer.h
 *  \brief Header file for puncturer class.
 *
 *  The puncturer class punctures the convolutionally encoded data to
 *  effectively increase the coding rate.  On the receive side it
 *  depunctures the received punctured data by inserting 0's into the
 *  "puncture holes".
 *
 */

#ifndef PUNCTURER_H
#define PUNCTURER_H

#include "rates.h"

namespace fun
{
    /*!
     * \brief The puncturer class
     *
     *  The puncturer class punctures the convolutionally encoded data to
     *  effectively increase the coding rate.  On the receive side it
     *  depunctures the received punctured data by inserting 0's into the
     *  "puncture holes".
     */
    class puncturer
    {
    public:

        /*!
         * \brief Punctures the convolutionally encoded data to get it to the desired coding rate.
         * \param data Vector of the convolutionally encoded data to be punctured.
         * \param rate_params The parameters for the PHY Rate from which the coding rate is extracted.
         * \return Vector of the punctured data.
         */
        static std::vector<unsigned char> puncture(std::vector<unsigned char> data, RateParams rate_params);

        /*!
        * \brief depunctures the data by inserting 0's in the "puncture holes"
        * \param data Vector of the punctured data to be depunctured.
        * \param rate_params The parameters for the PHY Rate from which the coding rate is extracted.
        * \return Vector of the depunctured data.
        */
        static std::vector<unsigned char> depuncture(std::vector<unsigned char> data, RateParams rate_params);
    };
}


#endif // PUNCTURER_H
