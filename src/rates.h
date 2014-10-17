/*! \file rates.h
 *  \brief Header file for the PHY Rates parameters and the RateParams struct.
 *
 *  The PHY rate parameters are used by the PPDU class to correctly encode and decode
 *  PHY frames.  The RateParams struct holds all the necessary parameters for each PPDU
 *  and contains constructors for getting the necessary parameters either from the desired
 *  transmit rate or from the rate field in the received PHY frame.
 */

#ifndef RATES_H
#define RATES_H

#include <assert.h>
#include <vector>
#include <string>

namespace fun
{    

    /*! \brief Valid rate field values */
    static std::vector<unsigned char> VALID_RATES = {0xD, 0xE, 0xF, 0x5, 0x6, 0x7, 0x9, 0xA, 0xB, 0x1, 0x3};


    /*! \brief An enum for referencing the different PHY data rates by name.
     *
     * The numbers represent the coding rate used with the first number being the
     * numerator and the second number being the denominator followed by the modulation
     * used. For example RATE_1_2_BPSK means BPSK modulation with a Rate 1/2
     * convoluational code.
     */
    enum Rate : int
    {
        RATE_1_2_BPSK = 0,  //!< Rate 1/2 code : BPSK modulation
        RATE_2_3_BPSK = 1,  //!< Rate 2/3 code : BPSK modulation
        RATE_3_4_BPSK = 2,  //!< Rate 3/4 code : BPSK modulation
        RATE_1_2_QPSK = 3,  //!< Rate 1/2 code : QPSK modulation
        RATE_2_3_QPSK = 4,  //!< Rate 2/3 code : QPSK modulation
        RATE_3_4_QPSK = 5,  //!< Rate 3/4 code : QPSK modulation
        RATE_1_2_QAM16 = 6, //!< Rate 1/2 code : QAM16 modulation
        RATE_2_3_QAM16 = 7, //!< Rate 2/3 code : QAM16 modulation
        RATE_3_4_QAM16 = 8, //!< Rate 3/4 code : QAM16 modulation
        RATE_2_3_QAM64 = 9, //!< Rate 2/3 code : QAM64 modulation
        RATE_3_4_QAM64 = 10 //!< Rate 3/4 code : QAM64 modulation
    };

    // Parameters for each data rate
    /*!
     * \brief The RateParams struct
     *
     * Parameters for each data rate
     */
    struct RateParams
    {
        unsigned char rate_field; //!< SIGNAL rate field
        int cbps;                 //!< Coded bits per symbol
        int dbps;                 //!< Data bits per symbol
        int bpsc;                 //!< Bits per subcarrier
        Rate rate;                //!< Rate enum value
        double rel_rate;          //!< Relative coding rate (relative to 1/2)
        std::string name;         //!< Display name

        /*!
         * \brief RateParams constructor
         *
         * Populates the rate parameters appropriately for the given PHY Rate
         * \param _rate the PHY Rate for which the corresponding parameters are desired
         */
        RateParams(Rate _rate)
        {
            switch(_rate)
            {
                // 1/2 BPSK
                case RATE_1_2_BPSK:
                    rate_field = 0xD;
                    cbps = 48;
                    dbps = 24;
                    rate = RATE_1_2_BPSK;
                    rel_rate = 1.0;
                    bpsc = 1;
                    name = "1/2 BPSK";
                    break;

                // 2/3 BPSK
                case RATE_2_3_BPSK:
                    rate_field = 0xE;
                    cbps = 48;
                    dbps = 32;
                    rate = RATE_2_3_BPSK;
                    rel_rate = 3.0 / 4.0;
                    bpsc = 1;
                    name = "2/3 BPSK";
                    break;

                // 3/4 BPSK
                case RATE_3_4_BPSK:
                    rate_field = 0xF;
                    cbps = 48;
                    dbps = 36;
                    rate = RATE_3_4_BPSK;
                    rel_rate = 2.0 / 3.0;
                    bpsc = 1;
                    name = "3/4 BPSK";
                    break;

                // 1/2 QPSK
                case RATE_1_2_QPSK:
                    rate_field = 0x5;
                    cbps = 96;
                    dbps = 48;
                    rate = RATE_1_2_QPSK;
                    rel_rate = 1.0;
                    bpsc = 2;
                    name = "1/2 QPSK";
                    break;

                // 2/3 QPSK
                case RATE_2_3_QPSK:
                    rate_field = 0x6;
                    cbps = 96;
                    dbps = 64;
                    rate = RATE_2_3_QPSK;
                    rel_rate = 3.0 / 4.0;
                    bpsc = 2;
                    name = "2/3 QPSK";
                    break;

                // 3/4 QPSK
                case RATE_3_4_QPSK:
                    rate_field = 0x7;
                    cbps = 96;
                    dbps = 72;
                    rate = RATE_3_4_QPSK;
                    rel_rate = 2.0 / 3.0;
                    bpsc = 2;
                    name = "3/4 QPSK";
                    break;

                // 1/2 QAM16
                case RATE_1_2_QAM16:
                    rate_field = 0x9;
                    cbps = 192;
                    dbps = 96;
                    rate = RATE_1_2_QAM16;
                    rel_rate = 1.0;
                    bpsc = 4;
                    name = "1/2 QAM16";
                    break;

                // 2/3 QAM16
                case RATE_2_3_QAM16:
                    rate_field = 0xA;
                    cbps = 192;
                    dbps = 128;
                    rate = RATE_2_3_QAM16;
                    rel_rate = 3.0 / 4.0;
                    bpsc = 4;
                    name = "2/3 QAM16";
                    break;

                // 3/4 QAM16
                case RATE_3_4_QAM16:
                    rate_field = 0xB;
                    cbps = 192;
                    dbps = 144;
                    rate = RATE_3_4_QAM16;
                    rel_rate = 2.0 / 3.0;
                    bpsc = 4;
                    name = "3/4 QAM16";
                    break;

                // 2/3 QAM64
                case RATE_2_3_QAM64:
                    rate_field = 0x1;
                    cbps = 288;
                    dbps = 192;
                    rate = RATE_2_3_QAM64;
                    rel_rate = 3.0 / 4.0;
                    bpsc = 6;
                    name = "2/3 QAM64";
                    break;

                // 3/4 QAM64
                case RATE_3_4_QAM64:
                    rate_field = 0x3;
                    cbps = 288;
                    dbps = 216;
                    rate = RATE_3_4_QAM64;
                    rel_rate = 2.0 / 3.0;
                    bpsc = 6;
                    name = "3/4 QAM64";
                    break;

                default:
                    break;
            }
        }

        /*!
         * \brief Gets a #RateParams instance based on the rate field in
         * the packet header.
         *
         * This function is used to get the appropriate rate parameters for the
         * received packet based on the rate field in the received packet header.
         *
         * \param rate_field The rate field bits from the packet header
         * \return #RateParams object with the appropriate parameters for that rate
         */
        static RateParams FromRateField(unsigned char rate_field)
        {
            switch(rate_field)
            {
                // 1/2 BPSK
                case 0xD: return RateParams(RATE_1_2_BPSK); break;

                // 2/3 BPSK
                case 0xE: return RateParams(RATE_2_3_BPSK); break;

                // 3/4 BPSK
                case 0xF: return RateParams(RATE_3_4_BPSK); break;

                // 1/2 QPSK
                case 0x5: return RateParams(RATE_1_2_QPSK); break;

                // 2/3 QPSK
                case 0x6: return RateParams(RATE_2_3_QPSK); break;

                // 3/4 QPSK
                case 0x7: return RateParams(RATE_3_4_QPSK); break;

                // 1/2 QAM16
                case 0x9: return RateParams(RATE_1_2_QAM16); break;

                // 2/3 QAM16
                case 0xA: return RateParams(RATE_2_3_QAM16); break;

                // 3/4 QAM16
                case 0xB: return RateParams(RATE_3_4_QAM16); break;

                // 2/3 QAM64
                case 0x1: return RateParams(RATE_2_3_QAM64); break;

                // 3/4 QAM64
                case 0x3: return RateParams(RATE_3_4_QAM64); break;

                default:
                    assert(false);
                    break;
            }
        }
    };
}



#endif // RATES_H
