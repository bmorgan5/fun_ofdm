/*! \file payload.cpp
 *  \brief C++ file for the sources of the data that fun-ofdm sends.
 */

#include <algorithm>
#include <cstring>
#include "payload.h"

namespace fun
{
    random_source::random_source(std::optional<std::uint64_t> limit) :
        m_remaining(limit),
        m_random(std::random_device()())
    {
    }

    std::size_t random_source::read(unsigned char * buffer, std::size_t max_bytes)
    {
        std::size_t count = max_bytes;
        if(m_remaining) count = std::min<std::uint64_t>(count, *m_remaining);

        // Fill 8 bytes at a time from the 64-bit generator
        for(std::size_t i = 0; i < count; i += 8)
        {
            std::uint64_t value = m_random();
            std::memcpy(buffer + i, &value, std::min<std::size_t>(8, count - i));
        }

        if(m_remaining) *m_remaining -= count;
        return count;
    }
}
