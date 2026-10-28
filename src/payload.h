/*! \file payload.h
 *  \brief Header file for the sources of the data that fun-ofdm sends.
 */

#ifndef PAYLOAD_H
#define PAYLOAD_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>

namespace fun
{
    /*!
     * \brief A stream of bytes to send.
     */
    class data_source
    {
    public:

        virtual ~data_source() = default;

        /*!
         * \brief Reads the next bytes of data.
         * \param buffer Where to put the bytes
         * \param max_bytes The most bytes to read
         * \return The number of bytes read, which is only less than max_bytes at the end of the
         *  data. 0 means there's no more data.
         */
        virtual std::size_t read(unsigned char * buffer, std::size_t max_bytes) = 0;
    };

    /*!
     * \brief Random bytes, either a fixed number of them or an endless stream.
     */
    class random_source : public data_source
    {
    public:

        /*!
         * \brief Constructor for random_source
         * \param limit How many bytes to produce, or std::nullopt for no limit
         */
        explicit random_source(std::optional<std::uint64_t> limit);

        std::size_t read(unsigned char * buffer, std::size_t max_bytes) override;

    private:

        std::optional<std::uint64_t> m_remaining; //!< Bytes left to produce, or std::nullopt for no limit

        std::mt19937_64 m_random; //!< Random number generator, seeded from std::random_device
    };
}

#endif // PAYLOAD_H
