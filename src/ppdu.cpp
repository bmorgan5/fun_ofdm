/*! \file ppdu.cpp
 *  \brief C++ file for the ppdu class.
 *
 * The ppdu class is a container for a ppdu which is made up of a PHY Header,
 * otherwise known as a PLCP Header, and a payload of bytes. The class also
 * has functions for encoding and decoding the header and payload based on the
 * PHY transmission rates.
 */

#include <arpa/inet.h>
#include <boost/crc.hpp>
#include <iostream>

#include "ppdu.h"
#include "parity.h"
#include "viterbi.h"
#include "interleaver.h"
#include "puncturer.h"
#include "modulator.h"

namespace fun
{
    /*!
     * This constructor creates an empty PPDU with the default/empty plcp_header constructor
     */
    ppdu::ppdu()
    {
        header = plcp_header();
        payload.reserve(MAX_FRAME_SIZE);
    }

    /*!
     * This constructor creates a PPDU with a header, but no payload field.
     */
    ppdu::ppdu(Rate rate, int length)
    {
        RateParams rate_params = RateParams(rate);
        int num_symbols = std::ceil(
                double((16 /* service */ + 8 * (length + 4 /* CRC */) + 6 /* tail */)) /
                double(rate_params.dbps));
        header = plcp_header(rate, length, num_symbols);
        payload.reserve(MAX_FRAME_SIZE);
    }


    /*!
     * This constructor creates a complete PPDU with header and payload.
     */
    ppdu::ppdu(std::vector<unsigned char> payload, Rate rate) :
        payload(payload)
    {
        RateParams rate_params = RateParams(rate);
        int length = payload.size();
        int num_symbols = std::ceil(
                double((16 /* service */ + 8 * (length + 4 /* CRC */) + 6 /* tail */)) /
                double(rate_params.dbps));

        header = plcp_header(rate, length, num_symbols);
    }

    /*!
     * Public wrapper for encoding the header & payload and concatenating them together into a
     * PHY frame.
     */
    std::vector<std::complex<double> > ppdu::encode()
    {
        std::vector<std::complex<double> > header_samples = encoder_header();
        std::vector<std::complex<double> > payload_samples = encode_data();
        std::vector<std::complex<double> > ppdu_samples = std::vector<std::complex<double> >(header_samples.size() + payload_samples.size());
        memcpy(&ppdu_samples[0], &header_samples[0], header_samples.size() * sizeof(std::complex<double>));
        memcpy(&ppdu_samples[48], payload_samples.data(), payload_samples.size() * sizeof(std::complex<double>));
        return ppdu_samples;
    }


    /*!
     * Uses the rate_params to build the header. Note the header is NOT scrambled.
     * Codes the header using a 1/2 convolutional code. Interleaves the header. And finally
     * modulates the header using BPSK modulation.
     */
    std::vector<std::complex<double> > ppdu::encoder_header()
    {
        // Build the header from the rate field and length
        RateParams rate_params = RateParams(header.rate);
        unsigned int header_field = 0;
        header_field = ((rate_params.rate_field & 0xF) << 13) | (header.length & 0xFFF);

        // Set the parity bit and align
        if(parity(header_field) == 1) header_field |= 131072;
        header_field <<= 6;

        // Convert the header to a unsigned char array
        unsigned char header_bytes[4];
        unsigned int h = htonl(header_field) >> 8;
        memcpy(header_bytes, &h, 3);

        // Convolutionally encode the header
        std::vector<unsigned char> header_symbols(48 /* header is always a single 1/2 BPSK symbol */);
        viterbi v;
        v.conv_encode(header_bytes, &header_symbols[0], 18 /* header is always 18 data bits */);

        // Interleave the header
        std::vector<unsigned char> interleaved = interleaver::interleave(header_symbols);

        // Modulate the header
        std::vector<std::complex<double> > modulated = modulator::modulate(interleaved, RATE_1_2_BPSK);

        return modulated;

    }

    std::vector<std::complex<double> > ppdu::encode_data()
    {
        // Get the RateParams
        RateParams rate_params = RateParams(header.rate);

        // Calculate the number of symbols
        int num_symbols = std::ceil(
                double((16 /* service */ + 8 * (payload.size() + 4 /* CRC */) + 6 /* tail */)) /
                double(rate_params.dbps));

        // Calculate the number of data bits/bytes (including padding bits)
        int num_data_bits = num_symbols * rate_params.dbps;
        int num_data_bytes = num_data_bits / 8;

        unsigned short service_field = 0;

        // Concatenate the service and payload
        std::vector<unsigned char> data(num_data_bytes+1, 0);
        memcpy(&data[0], &service_field, 2);
        memcpy(&data[2], payload.data(), payload.size());

        // Calcualate and append the CRC
        boost::crc_32_type crc;
        crc.process_bytes(&data[0], 2 + payload.size());
        unsigned int calculated_crc = crc.checksum();
        memcpy(&data[2 + payload.size()], &calculated_crc, 4);

        // Scramble the data
        std::vector<unsigned char> scrambled(num_data_bytes+1, 0);
        int state = 93, feedback = 0;
        for(int x = 0; x < num_data_bytes; x++)
        {
           feedback = (!!(state & 64)) ^ (!!(state & 8));
           scrambled[x] = feedback ^ data[x];
           state = ((state << 1) & 0x7E) | feedback;
        }
        data.swap(scrambled);

        // Convolutionally encode the data
        std::vector<unsigned char> data_encoded(num_data_bits * 2, 0);
        viterbi v;
        v.conv_encode(&data[0], data_encoded.data(), num_data_bits-6);

        // Puncture the data
        std::vector<unsigned char> data_punctured = puncturer::puncture(data_encoded, header.rate);

        // Interleave the data
        std::vector<unsigned char> data_interleaved = interleaver::interleave(data_punctured);

        // Modulated the data
        std::vector<std::complex<double> > data_modulated = modulator::modulate(data_interleaved, header.rate);

        return data_modulated;
    }

    // Decode a PLCP header from 48 complex samples
    bool ppdu::decode_header(std::vector<std::complex<double> > samples)
    {
        assert(samples.size() == 48);

        // Demodulate the header
        std::vector<unsigned char> demodulated = modulator::demodulate(samples, RATE_1_2_BPSK);

        // Deinterleave the header
        std::vector<unsigned char> deinterleaved = interleaver::deinterleave(demodulated);

        // Convolutionally decode the header        
        std::vector<unsigned char> header_bytes(4);
        viterbi v;
        v.conv_decode(deinterleaved.data(), header_bytes.data(), 18 /* header is always 18 data bits */);

        // Verify header parity
        unsigned int header_field;
        memcpy(&header_field, &header_bytes[0], 3);
        header_field = htonl(header_field) >> 8;
        int par = parity(header_field);
        if(par == 1)
        {
            return false;
        }

        // Get the rate field and length
        unsigned char rate_field = ((header_field >> 19) & 0xF);
        unsigned int length = ((header_field >> 6) & 0xFFF);

        // Check for a valid rate
        bool valid_rate = false;
        for(int x = 0; x < VALID_RATES.size(); x++) if(VALID_RATES[x] == rate_field) valid_rate = true;
        if(!valid_rate)
        {
            return false;
        }

        // Calculate the number of symbols
        RateParams rate_params = RateParams::FromRateField(rate_field);
        int num_symbols = std::ceil(
                double((16 /* service */ + 8 * (length + 4 /* CRC */) + 6 /* tail */)) /
                double(rate_params.dbps));

        // Populate the header fields
        header.length = length;
        header.rate = RateParams::FromRateField(rate_field).rate;
        header.num_symbols = num_symbols;

        // Indicate success
        return true;
    }




    bool ppdu::decode_data(std::vector<std::complex<double> > samples)
    {
        // Get the RateParams
        RateParams rate_params = RateParams(header.rate);

        // Calculate the number of symbols
        int num_symbols = std::ceil(
                double((16 /* service */ + 8 * (header.length + 4 /* CRC */) + 6 /* tail */)) /
                double(rate_params.dbps));

        // Calculate the number of data bits/bytes (including padding bits)
        int num_data_bits = num_symbols * rate_params.dbps;
        int num_data_bytes = num_data_bits / 8;

        // Demodulate the data
        std::vector<unsigned char> demodulated = modulator::demodulate(samples, header.rate);

        // Deinterleave the data
        std::vector<unsigned char> deinterleaved = interleaver::deinterleave(demodulated);

        // Depuncture the data
        std::vector<unsigned char> depunctured = puncturer::depuncture(deinterleaved, header.rate);

        // Convolutionally decode the data
        int data_bits = 16 /* service */ + (header.length + 4 /* CRC */) * 8 + 6 /* tail bits */;
        int data_bytes = data_bits / 8 + 1;
        data_bits = num_data_bits - 6;
        data_bytes = num_data_bytes;
        std::vector<unsigned char> decoded(data_bytes);
        viterbi v;
        v.conv_decode(&depunctured[0], &decoded[0], data_bits);

        // Descramble the data
        std::vector<unsigned char> descrambled(num_data_bytes+1, 0);
        int state = 93, feedback = 0;
        for(int x = 0; x < num_data_bytes; x++)
        {
           feedback = (!!(state & 64)) ^ (!!(state & 8));
           descrambled[x] = feedback ^ decoded[x];
           state = ((state << 1) & 0x7E) | feedback;
        }
        decoded.swap(descrambled);

        // Calculate the CRC
        boost::crc_32_type crc;
        crc.process_bytes(&decoded[0], 2 + header.length);
        unsigned int calculated_crc = crc.checksum();
        unsigned int given_crc = 0;
        memcpy(&given_crc, &decoded[2 + header.length], 4);

        // Verify the CRC
        if(given_crc != calculated_crc)
        {
            std::cerr << "Invalid CRC (length " << header.length << ")" << std::endl;
            // Indicate failure
            return false;
        }
        else
        {
            // Copy the payload
    //        std::vector<unsigned char> payload(length);
            payload.resize(header.length);
            memcpy(&payload[0], &decoded[2 /* skip the service field */], header.length);

            // Fill the output values
    //        data_out.rate = rate;
            memcpy(&header.service, &decoded[0], 2);
    //        data_out.length = length;
            // Indicate success
            return true;
        }

    }

}

