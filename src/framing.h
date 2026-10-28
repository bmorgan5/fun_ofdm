/*! \file framing.h
 *  \brief Header file for splitting a stream of data into frames and putting it back together.
 *
 *  Each frame's payload starts with a fun::FRAME_HEADER_SIZE byte header holding the frame's
 *  sequence number (big-endian), followed by up to fun::FRAME_DATA_SIZE bytes of data. The PHY's
 *  CRC-32 already rejects corrupted frames, so the sequence numbers are only used to detect lost
 *  and duplicate frames and to know where each frame's data belongs.
 */

#ifndef FRAMING_H
#define FRAMING_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#include "payload.h"

namespace fun
{
    /*!
     * \brief The size of the sequence number at the start of each frame's payload.
     */
    const std::size_t FRAME_HEADER_SIZE = 4;

    /*!
     * \brief The most data bytes in a frame, so each frame's payload is at most 1500 bytes.
     *
     *  802.11a allows payloads of up to 4095 bytes, but longer frames lose more data to each
     *  error and drift further out of phase over the air.
     */
    const std::size_t FRAME_DATA_SIZE = 1496;

    /*!
     * \brief Splits the data from a source into frame payloads.
     */
    class fragmenter
    {
    public:

        /*!
         * \brief Constructor for fragmenter
         * \param source The data to split into frames
         */
        explicit fragmenter(data_source & source);

        /*!
         * \brief Builds the next frame's payload: its sequence number followed by its data.
         * \return The payload, or an empty vector once the source has no more data.
         */
        std::vector<unsigned char> next_frame();

    private:

        data_source & m_source; //!< The data to split into frames

        std::uint32_t m_sequence = 0; //!< The next frame's sequence number
    };

    /*!
     * \brief A frame's sequence number and data, taken from its payload.
     */
    struct received_frame
    {
        std::uint32_t sequence;           //!< The frame's sequence number
        std::vector<unsigned char> data;  //!< The frame's data, without the header
    };

    /*!
     * \brief Checks the sequence numbers of received frames, counting lost and duplicate frames.
     *
     *  Frames are sent over a single link, so they arrive in order or not at all. A sequence number
     *  past the next one expected means the frames in between were lost, and one before it is a
     *  duplicate. Counting starts from sequence number 0, so frames sent before the receiver
     *  started also count as lost.
     */
    class reassembler
    {
    public:

        /*!
         * \brief Checks a received frame.
         * \param payload The frame's payload
         * \return The frame's sequence number and data if it is a new frame, or std::nullopt if it
         *  is a duplicate or too short to hold a header.
         */
        std::optional<received_frame> add(const std::vector<unsigned char> & payload);

        /*!
         * \brief The number of frames found to be lost so far.
         */
        std::uint64_t lost() const;

        /*!
         * \brief The number of duplicate frames received so far.
         */
        std::uint64_t duplicates() const;

    private:

        std::uint64_t m_next_sequence = 0; //!< The sequence number expected next

        std::uint64_t m_lost = 0; //!< Frames lost so far

        std::uint64_t m_duplicates = 0; //!< Duplicate frames received so far
    };
}

#endif // FRAMING_H
