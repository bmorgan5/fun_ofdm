/*! \file symbol_mapper.cpp
 *  \brief C++ file for the symbol mapper class.
 *
 *  The symbol mapper class takes the stream of modulated data and converts it
 *  into symbols by mapping the data, pilots, and nulls onto their respective
 *  subcarriers. Conversely it also extracts the data from received symbols.
 */

#include <cstring>
#include <assert.h>

#include "symbol_mapper.h"

namespace fun
{

    /*!
     * The map for where the data, pilots, and null subcarriers go.
     * - Legend:
     *  + 0 -> null
     *  + 1 -> data
     *  + 2 -> pilot
     */
    const std::vector<unsigned char> symbol_mapper::m_active_map =
    {
        0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2,
        1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 2, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0
    };

    /*!
     *  The polarity sequence that is multiplied by the pilot sample for each OFDM symbol beginning
     *  with the signal symbol. For example, the pilots in the signal symbol will all be multipled by
     *  POLARITY[0] (i.e. +1). The next symbol will be multipled by POLARITY[1], and so on.
     *  If the number of symbols is longer than 127 then the sequence just wraps back around to the
     *  beginning. The modulus (%) operator is very useful for achieving this effect.
     */
    const double symbol_mapper::POLARITY[127] =
    {
             1, 1, 1, 1,-1,-1,-1, 1,-1,-1,-1,-1, 1, 1,-1, 1,
            -1,-1, 1, 1,-1, 1, 1,-1, 1, 1, 1, 1, 1, 1,-1, 1,
             1, 1,-1, 1, 1,-1,-1, 1, 1, 1,-1, 1,-1,-1,-1, 1,
            -1, 1,-1,-1, 1,-1,-1, 1, 1, 1, 1, 1,-1,-1, 1, 1,
            -1,-1, 1,-1, 1,-1, 1, 1,-1,-1,-1, 1, 1,-1,-1,-1,
            -1, 1,-1,-1, 1,-1, 1, 1, 1, 1,-1, 1,-1, 1,-1, 1,
            -1,-1,-1,-1,-1, 1,-1, 1, 1,-1, 1,-1, 1, 1, 1,-1,
            -1, 1,-1,-1,-1, 1, 1, 1,-1,-1,-1,-1,-1,-1,-1
    };

    /*!
     * The pilots are always BPSK modulated, or in other words they are always
     * (1 + 0j) or (-1 + 0j).
     * Also, the first three pilots are always the same with the 4th pilot being inverted.
     */
    const std::complex<double> symbol_mapper::PILOTS[4] =
    {
        { 1, 0},
        { 1, 0},
        { 1, 0},
        {-1, 0}
    };

    /*!
     *  -Initializations
     *  + #m_data_subcarrier_count -> 48 data subcarriers
     *  + #m_pilot_count -> 4 pilot subcarriers
     */
    symbol_mapper::symbol_mapper() :
        m_data_subcarrier_count(48),
        m_pilot_count(4)
    {
    }

    /*!
     *  Takes a vector of modulated data samples and maps them into OFDM symbols. The vector of
     *  data samples must be an integer multiple of 48 so that an integer number of symbols can be
     *  created since there are 48 data subcarriers. It also inserts the 4 pilot subcarriers and 12
     *  null subcarriers. The output is a vector of samples with each set of 64 samples constituting
     *  one symbol.
     */
    std::vector<std::complex<double> > symbol_mapper::map(std::vector<std::complex<double> > data_samples)
    {
        assert(data_samples.size() % m_data_subcarrier_count == 0);

        std::complex<double> pilot_value = std::complex<double>(1, 0);
        std::complex<double> null_value = std::complex<double>(0, 0);

        std::vector<std::complex<double> > samples(data_samples.size() * m_active_map.size() / m_data_subcarrier_count);
        int out_index = 0, in_index = 0;
        int symbol_count = 0;

        for(int x = 0; x < data_samples.size(); x+= m_data_subcarrier_count)
        {
            int pilot_index = 0;
            for(int s = 0; s < m_active_map.size(); s++)
            {
                switch(m_active_map[s])
                {
                    // Null subcarrier
                    case 0:
                        samples[out_index++] = null_value;
                        break;

                    // Data subcarrier
                    case 1:
                        samples[out_index++] = data_samples[in_index++];
                        break;

                    // Pilot subcarrier
                    case 2:
                        samples[out_index++] = PILOTS[pilot_index++] * POLARITY[symbol_count % 127];
                        break;
                }
            }
            symbol_count++;
        }

        return samples;
    }

    // Remove pilots and null subcarriers, leaving only data subcarriers
    /*!
     *  Takes in a vector of samples and extracts the 48 data subcarriers while throwing away the nulls
     *  and the pilots. The input must be an integer multiple of 64 (the size of one symbol) in length
     *  so that we do not have partial symbols which wouldn't make sense. The output is simply a stream
     *  of received data however it will be an integer multiple of 48.
     */
    std::vector<std::complex<double> > symbol_mapper::demap(std::vector<std::complex<double> > samples)
    {
        assert(samples.size() % m_active_map.size() == 0);

        std::vector<std::complex<double> > data_samples(samples.size() * m_data_subcarrier_count / m_active_map.size());
        int out_index = 0;
        for(int x = 0; x < samples.size(); x++)
        {
            int index = x % m_active_map.size();
            if(m_active_map[index] == 1)
                data_samples[out_index++] = samples[x];
        }

        return data_samples;
    }

    // Get the active subcarrier map
    std::vector<unsigned char> symbol_mapper::get_active_map()
    {
        return m_active_map;
    }

}

