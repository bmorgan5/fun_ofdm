/*! \file framing.cpp
 *  \brief C++ file for splitting a stream of data into frames and putting it back together.
 */

#include "framing.h"

namespace fun
{
    fragmenter::fragmenter(data_source & source) :
        m_source(source)
    {
    }

    std::vector<unsigned char> fragmenter::next_frame()
    {
        std::vector<unsigned char> payload(FRAME_HEADER_SIZE + FRAME_DATA_SIZE);
        std::size_t count = m_source.read(&payload[FRAME_HEADER_SIZE], FRAME_DATA_SIZE);
        if(count == 0) return {};

        payload.resize(FRAME_HEADER_SIZE + count);
        for(std::size_t i = 0; i < FRAME_HEADER_SIZE; i++)
        {
            payload[i] = (m_sequence >> (8 * (FRAME_HEADER_SIZE - 1 - i))) & 0xFF;
        }
        m_sequence++;

        return payload;
    }

    std::optional<received_frame> reassembler::add(const std::vector<unsigned char> & payload)
    {
        if(payload.size() < FRAME_HEADER_SIZE) return std::nullopt;

        std::uint32_t sequence = 0;
        for(std::size_t i = 0; i < FRAME_HEADER_SIZE; i++) sequence = (sequence << 8) | payload[i];

        if(sequence < m_next_sequence)
        {
            m_duplicates++;
            return std::nullopt;
        }

        m_lost += sequence - m_next_sequence;
        m_next_sequence = sequence + 1;

        return received_frame{sequence, std::vector<unsigned char>(payload.begin() + FRAME_HEADER_SIZE, payload.end())};
    }

    std::uint64_t reassembler::lost() const
    {
        return m_lost;
    }

    std::uint64_t reassembler::duplicates() const
    {
        return m_duplicates;
    }
}
