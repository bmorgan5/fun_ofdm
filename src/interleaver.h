/*! \file interleaver.h
 *  \brief Header file for the Interleaver class and the BitInterleave struct.
 *
 * The interleaver class performs interleaving as described in section 17.3.5.6 of
 * the 802.11a-1999 standard. The Interleaver class contains two static functions:
 * interleave and deinterleave and thus doesn't need a constructor. However, it does
 * use the BitInterleave struct as a helper for these two functions.
 *
 */

#ifndef INTERLEAVER_H
#define INTERLEAVER_H

#include <stdlib.h>
#include <vector>
#include <cassert>

#include "rates.h"

namespace fun
{

    /*!
     * \brief The interleaver class
     *
     * The interleaver class performs interleaving as described in section 17.3.5.6 of
     * the 802.11a-1999 standard. The Interleaver class contains two static functions:
     * interleave and deinterleave and thus doesn't need a constructor. However, it does
     * use the BitInterleave struct as a helper for these two functions.
     */
    class interleaver
    {
    public:

        /*!
         * \brief interleaves the data
         * \param data Vector of data to be interleaved
         * \return Vector of interleaved data
         */
        static std::vector<unsigned char> interleave(std::vector<unsigned char> data);

        /*!
         * \brief deinterleaves the data
         * \param data Vector of data to be deinterleaved
         * \return Vector of deinterleaved data
         */
        static std::vector<unsigned char> deinterleave(std::vector<unsigned char> data);

    };

    /*!
     * \brief The BitInterleave struct
     *
     * This struct sets up the interleaving map based on the the number of coded bits per symbol
     */
    struct BitInterleave {
        unsigned int d_bpsc; // coded bits per subcarrier
        unsigned int d_cbps; // coded bits per symbol
        static const int d_num_chunks = 16;

        BitInterleave(int ncarriers, int nbits) :
            d_bpsc(nbits),
            d_cbps(nbits * ncarriers)
        {}

        unsigned int index(unsigned int k)
        {
            // see 17.3.5.6 in 802.11a-1999, floor is implicit
            assert (k < d_cbps);
            unsigned int s = std::max(d_bpsc / 2, (unsigned int)1);
            unsigned int i = (d_cbps / d_num_chunks) * (k % d_num_chunks) + (k / d_num_chunks);
            unsigned int j = s * (i / s) + (i + d_cbps - (d_num_chunks * i / d_cbps)) % s;
            assert (j < d_cbps);
            return j;
        }

        void fill(std::vector<unsigned int> &v, bool inverse)
        {
          v.resize(d_cbps);
          if (!inverse)
          {
              for (unsigned int i = 0; i < d_cbps; ++i)
              {
                  v[i] = index(i);
              }
          } 
          else
          {
              for (unsigned int i = 0; i < d_cbps; ++i)
              {
                  v[index(i)] = i;
              }
          }
        }
    };
}


#endif // INTERLEAVER_H
