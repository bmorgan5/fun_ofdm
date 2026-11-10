/*! \file payload.h
 * \brief Header file for the sources of the data that fun-ofdm sends.
 */

#ifndef PAYLOAD_H
#define PAYLOAD_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>

namespace fun {
/*!
 * \brief A stream of bytes to send.
 */
class data_source {
 public:
  virtual ~data_source() = default;

  /*!
   * \brief Reads the next bytes of data.
   * \param buffer Where to put the bytes
   * \param max_bytes The most bytes to read
   * \return The number of bytes read, which is only less than max_bytes at the
   * end of the data. 0 means there's no more data.
   */
  virtual std::size_t read(std::uint8_t* buffer, std::size_t max_bytes) = 0;
};

/*!
 * \brief Makes a random seed for a run that wasn't given one.
 * \return 64 bits from std::random_device
 */
std::uint64_t random_seed();

/*!
 * \brief Random bytes, either a fixed number of them or an endless stream.
 *
 * The bytes are std::mt19937_64's output, 8 bytes per number, least
 * significant byte first. The standard defines that generator exactly, so a
 * seed gives the same bytes with any compiler and on any machine.
 */
class random_source : public data_source {
 public:
  /*!
   * \brief Constructor for random_source
   * \param limit How many bytes to produce, or std::nullopt for no limit
   * \param seed The seed for the random number generator
   */
  random_source(std::optional<std::uint64_t> limit, std::uint64_t seed);

  std::size_t read(std::uint8_t* buffer, std::size_t max_bytes) override;

 private:
  //! Bytes left to produce, or std::nullopt for no limit
  std::optional<std::uint64_t> m_remaining;

  //! Random number generator
  std::mt19937_64 m_random;
};
}  // namespace fun

#endif  // PAYLOAD_H
