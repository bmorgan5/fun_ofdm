/*! \file interleaver.cpp
 *  \brief C++ file for the Interleaver class.
 *
 * The interleaver class performs interleaving as described in section 17.3.5.6 of
 * the 802.11a-1999 standard. The Interleaver class contains two static functions:
 * interleave and deinterleave and thus doesn't need a constructor. However, it does
 * use the BitInterleave struct as a helper for these two functions.
 */

#include "interleaver.h"

namespace fun
{
    // Interleave some data
    std::vector<unsigned char> interleaver::interleave(std::vector<unsigned char> data)
    {
        std::vector<unsigned int> interleave_map;
        BitInterleave(48, 1).fill(interleave_map, false);

        std::vector<unsigned char> data_interleaved(data.size());
        for(int x = 0; x < data.size(); x += interleave_map.size())
            for(int y = 0; y < interleave_map.size(); y++)
                data_interleaved[x + interleave_map[y]] = data[x + y];
        return data_interleaved;
    }

    // Deinterleave some data
    std::vector<unsigned char> interleaver::deinterleave(std::vector<unsigned char> data)
    {
        std::vector<unsigned int> deinterleave_map;
        BitInterleave(48, 1).fill(deinterleave_map, true);

        std::vector<unsigned char>data_deinterleaved(data.size());
        for(int s = 0; s < data.size(); s += deinterleave_map.size())
            for(int t = 0; t < deinterleave_map.size(); t++)
                data_deinterleaved[s + deinterleave_map[t]] = data[s + t];
        return data_deinterleaved;
    }
}

