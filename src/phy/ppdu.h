/*! \file ppdu.h
 *  \brief The SIGNAL field and data of a frame, and their coding.
 *
 *  A PPDU is what follows the preamble: one SIGNAL symbol, then the data
 *  symbols (802.11a-1999 section 17.3.2). This class codes both into
 *  constellation points, 48 per symbol, and decodes them.
 *
 *  **SIGNAL** tells the receiver the data's rate and length. It's always rate
 *  1/2 BPSK, so it can be decoded first, and it isn't scrambled. Sizes are
 *  in bits:
 *
 *  ~~~
 *  ┌──────┬──────────┬────────┬────────┬──────┐
 *  │ rate │ reserved │ length │ parity │ tail │
 *  │  4   │    1     │   12   │   1    │  6   │  24 bits
 *  └──────┴──────────┴────────┴────────┴──────┘
 *  → encode (rate 1/2) → 48 coded bits → interleave → BPSK → 48 points
 *  ~~~
 *
 *  The length is the payload's length in bytes, up to 4095. The receiver
 *  rejects a header with the wrong parity or an unknown rate.
 *
 *  **Data** is one bit stream for the whole frame, coded and then cut into
 *  symbols, so there's one CRC however many symbols it takes. Sizes are in
 *  bits:
 *
 *  ~~~
 *  ┌─────────┬──────────────┬────────┬───────────────────────┬──────┐
 *  │ SERVICE │ payload      │ CRC-32 │ pad to a whole symbol │ tail │
 *  │ 16      │ 8 × length   │ 32     │                       │ 6    │
 *  └─────────┴──────────────┴────────┴───────────────────────┴──────┘
 *  → scramble → encode → puncture → interleave → modulate → 48 points/symbol
 *  ~~~
 *
 *  The symbol count is:
 *
 *  ~~~
 *  bits     = 16 + 8 × (length + 4) + 6     SERVICE + payload + CRC + tail
 *  symbols  = ceil(bits / dbps)
 *  ~~~
 *
 *  The scrambler XORs the data with a pseudo-random sequence, so long runs of
 *  identical bits don't turn into a few strong tones (see scrambler.h).
 *  Decoding reverses the steps: demodulate, deinterleave, depuncture, Viterbi
 *  decode, descramble, then check the CRC.
 *
 *  Unlike 802.11a:
 *  - The scrambler starts from a fixed state, steps once per byte, and only
 *    flips the lowest bit of each byte (see scrambler.h).
 *  - Bytes are sent most significant bit first, and the SIGNAL field's bits
 *    are in a different order.
 *  - The tail bits come after the padding instead of before it, and are
 *    scrambled instead of being set to zero after scrambling.
 *  - The PHY adds a CRC-32 to the payload, and checks and strips it on
 *    receive. In 802.11 the CRC-32 is the MAC's frame check sequence (FCS),
 *    already part of the PSDU that the PHY is given, and LENGTH counts it.
 *    Here LENGTH is the payload's length, without the CRC.
 *
 *  \bug The tail bits are scrambled, so they aren't always zeros, and the
 *  encoder doesn't always end in state 0. The Viterbi decoder assumes it
 *  does. The scrambler only flips the lowest bit of each byte, so at most one
 *  or two tail bits are 1, and in practice only the tail bits themselves
 *  decode wrong, so frames still get through. 802.11a sets the tail bits to
 *  zero after scrambling, and puts them before the padding.
 *
 *  \todo Move the CRC-32 out of the PHY, into the layer that calls it (the
 *  MAC, once there is one). Then the PSDU is exactly the bytes the PHY is
 *  given, LENGTH is their length, and the receiver passes up every PSDU whose
 *  SIGNAL field decodes, leaving the FCS check to the layer above, as 802.11a
 *  does.
 */

#ifndef PPDU_H
#define PPDU_H

#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "rates.h"

#define MAX_FRAME_SIZE 2000

namespace fun {
/*!
 * \brief The plcp_header struct is a container for PLCP Headers and their
 *  respective parameters.
 */
struct plcp_header {
  Rate rate;                //!< The PHY Rate for this frame.
  std::size_t length;       //!< The length of the payload in bytes.
  std::size_t num_symbols;  //!< The number of ofdm symbols in the frame.
  std::uint16_t service;    //!< The service field.

  /*!
   * \brief Constructor for empty plcp_header.
   *
   * -Initializations:
   *  + rate -> RATE_1_2_BPSK i.e. BPSK modulation with 1/2 rate convolutional
   * code
   *  + length -> 0
   *  + num_symbols -> 0
   *  + service -> 0
   */
  plcp_header() {
    rate = RATE_1_2_BPSK;
    length = 0;
    num_symbols = 0;
    service = 0;
  }

  /*!
   * \brief Constructor for plcp_header with rate and length known.
   * \param _rate PHY Rate for this frame.
   * \param _length Length of payload in bytes.
   */
  plcp_header(Rate _rate, std::size_t _length) {
    rate = _rate;
    length = _length;
    service = 0;
  }

  /*!
   * \brief Constructor for plcp_header with rate, length and number of symbols
   * known.
   * \param _rate PHY Rate for this frame.
   * \param _length Length of payload in bytes.
   * \param _num_symbols Number of OFDM symbols in frame.
   */
  plcp_header(Rate _rate, std::size_t _length, std::size_t _num_symbols) {
    rate = _rate;
    length = _length;
    num_symbols = _num_symbols;
    service = 0;
  }
};

/*!
 * \brief The ppdu class
 *
 * The ppdu class is a container for a ppdu which is made up of a PHY Header,
 * otherwise known as a PLCP Header, and a payload of bytes. The class also
 * has functions for encoding and decoding the header and payload based on the
 * PHY transmission rates.
 */
class ppdu {
 public:
  /****************
   * Constructors *
   ****************/

  /*!
   * \brief Default constructor for empty ppdu
   */
  ppdu();

  /*!
   * \brief Constructor for ppdu with rate and length known.
   * \param rate PHY Rate for this frame.
   * \param length Length of payload in bytes.
   */
  ppdu(Rate rate, std::size_t length);

  /*!
   * \brief Constructor for ppdu with payload and Rate known.
   * \param payload The payload/data/MPDU to be transmitted.
   * \param rate The PHY rate for this frame.
   */
  ppdu(std::vector<std::uint8_t> payload, Rate rate);

  /******************
   * Public Members *
   ******************/

  /*!
   * \brief Public interface for encoding a ppdu
   * \return Modulated data as a vector of complex doubles
   */
  std::vector<std::complex<double>> encode();

  /*!
   * \brief Public interface for decoding a plcp_header.
   * \param samples Complex samples representing the encoded header symbol.
   * \return boolean of whether decoding the header was successful or not
   *  based on checking/comparing the 1 bit parity field in the header.
   *  If successful the object's #header field is populated appropriately with
   *  the decoded fields.
   */
  bool decode_header(std::vector<std::complex<double>> samples);

  /*!
   * \brief Public interface for decoding the PHY payload into a PPDU.
   * \param samples Complex samples representing the encoded payload symbols.
   * \return boolean of whether decoding the payload was successful or not
   *  based on calculating and comparing the IEEE CRC-32 appended to the end
   *  of the payload. If successful the object's #payload field is populated
   *  with the decoded payload/MPDU.
   */
  bool decode_data(std::vector<std::complex<double>> samples);

  Rate get_rate() { return header.rate; }  //!< Get this PPDU's PHY tx rate
  std::size_t get_length() {
    return header.length;
  }  //!< Get this PPDU's payload length
  std::size_t get_num_symbols() {
    return header.num_symbols;
  }  //!< Get the number of OFDM symbols in this PPDU
  std::vector<std::uint8_t> get_payload() {
    return payload;
  }  //!< Get the payload of this PPDU.

 private:
  plcp_header header;                 //!< This PPDU's header parameters
  std::vector<std::uint8_t> payload;  //!< This PPDU's payload

  /*!
   * \brief Encodes this PPDU's header. The header is always encoded with
   *  BPSK modulation and 1/2 rate convolutional code.
   * \return The modulated header symbol.
   */
  std::vector<std::complex<double>> encoder_header();

  /*!
   * \brief Encodes this PPDU's payload. The payload is encoded at the rate
   *  specified in the header.rate field.
   * \return The modulated data.
   */
  std::vector<std::complex<double>> encode_data();
};

}  // namespace fun

#endif  // PPDU_H
