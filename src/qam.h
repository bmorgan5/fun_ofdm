/*! \file qam.h
 *  \brief Header file for QAM template.
 *
 * This file contains the QAM class which contains the
 * functions for encoding and decoding QAM modulation.
 *
 */

#ifndef QAM_H
#define QAM_H

#include <climits>

namespace fun
{

    /*! \brief Template for QAM class
     *
     * fast QAM (uses at most 4x imull to decode)
     * Tested on 7600 bogomips yields 600-1200Mbps encoding and 300Mbps decoding
     * Compile with -O3
     */
    template<int NumBits>
    class QAM
    {
        int d_gain;
        double d_scale_e;
        double d_scale_d;
    public:
        /*!
         * \brief QAM Constructor
         * \param power desired symbol power
         * \param gain gain on the decoded confidence (power of 2)
         */
        QAM (double power, int gain = 0)
        {
            d_gain = gain + CHAR_BIT - NumBits;
            const int nn = (1<<(NumBits-1));
            // sum((2k+1)^2,k=0..n-1)
            #if 0
            int sum2 = 0;
            for (int i = 0; i < nn; ++i) {
            sum2+= (2*i + 1)*(2*i + 1);
            }
            #else
            int sum2 = (4*nn*nn*nn-nn)/3;
            #endif
            double sf = sqrt(power * double(nn) / double(sum2));
            d_scale_e = sf;
            d_scale_d = (1 << d_gain) / sf;
        }

        /*!
         * \brief sign
         * \param v
         * \return +1 or -1
         */
        static inline int sign(int v)
        {
            return +1 | (v >> (sizeof(int) * CHAR_BIT - 1));
        }

        /*!
         * \brief clamp
         * \param i
         * \return saturate between 0 and 255
         */
        static inline int clamp(int i)
        {
            return i < 0 ? 0 : (i > 255 ? 255 : i);
        }

        /*!
         * \brief Encode recursively to match the decoding process
         *
         * This could have been implemented by a gray + multiply.
         * gray(i) = (i> >1)^i
         * see: http://www.dspguru.com/dsp/tricks/gray-code-conversion
         *
         * \param bits
         * \param sym
         */
        inline void encode (const char* bits, double *sym)
        {
            int pt = 0; // constellation point
            int flip = 1; // +1 or -1 -- for gray coding
            // unrolled with -O3
            for (int i = 0; i < NumBits; ++i)
                {
                int bit = *bits * 2 - 1; // +1 or -1
                pt = bit * flip + pt * 2;
                flip *= -bit;
                ++bits;
                }
            *sym = pt * d_scale_e;
        }

        /*!
         * \brief Decode recursively
         *
         * We decode recursively because we want meaningful confidences.
         * The alternative would be to simply divide and round, which only yields
         * the smallest per-bit confidence.
         *
         * output bit confidence is between 0 and 255.
         *
         * \param sym
         * \param bits
         */
        inline void decode (double sym, unsigned char *bits)
        {
            int pt = sym * d_scale_d;
            int flip = 1; // +1 or -1 -- for gray coding
            int amp = (1 << (NumBits-1)) << d_gain;
            // unrolled with -O3
            for (int i = 0; i < NumBits; ++i)
            {
                *bits = clamp(flip * pt + 128);
                int bit = sign(pt);
                pt -= bit * amp;
                flip = -bit;
                amp /= 2;
                ++bits;
            }
        }
    };
}

#endif // QAM_H
