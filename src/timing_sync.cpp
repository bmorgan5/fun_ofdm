/*! \file timing_sync.cpp
 *  \brief C++ file for the Timing Sync block.
 *
 *  This block is in charge of using the two LTS symbols to align the received frame in time.
 *  It also uses the two LTS symbols to perform an initial frequency offset estimation and
 *  applying the necessary correction.
 */


#include "timing_sync.h"

#include <algorithm>
#include <cstring>
#include <iostream>

#include "preamble.h"

namespace fun
{
    /*!
     * - Initializations:
     *   + #m_phase_acc -> 0.0
     *   + #m_phase_offset -> 0.0
     *   + #m_carryover -> 160 blank tagged samples
     */
    timing_sync::timing_sync() :
        block("timing_sync"),
        m_phase_acc(0),
        m_phase_offset(0),
        m_carryover(CARRYOVER_LENGTH, tagged_sample())
    {}

    int lts_count = 0;

    /*!
     * Once this block detects the #STS_END flag in the input samples it begins
     * correlating the input with the known #LTS_TIME_DOMAIN_CONJ samples to find
     * the two Long Training Sequence symbols. Once the start of the two symbols
     * are found it counts 8 samples backwards from the true start of the first
     * LTS symbol as #LTS1. This "tricks" the fft_symbols block into thinking that
     * each symbol begins earlier than it actually does. This is ok because each
     * symbol is prefaced by a cyclic prefix and due to the circular property of the
     * DFT it still works. This also aids in reliability in case the estimate of the
     * beginning of each symbol is slightly off.
     *
     * This block also uses the two LTS symbols to calculate an intial frequency offset.
     * It then applies the offset correction to all subsequent samples until the next
     * frame is detected and a new estimation is calculated.
     *
     */
    void timing_sync::work()
    {

        if(input_buffer.size() == 0) return;
        assert(input_buffer.size() > CARRYOVER_LENGTH);
        output_buffer.resize(input_buffer.size());

        std::vector<tagged_sample> input(input_buffer.size() + CARRYOVER_LENGTH);

        memcpy(&input[0],
               &m_carryover[0],
               CARRYOVER_LENGTH*sizeof(tagged_sample));

        memcpy(&input[CARRYOVER_LENGTH],
               &input_buffer[0],
               input_buffer.size() * sizeof(tagged_sample));

        for(int x = 0; x < input.size() - CARRYOVER_LENGTH; x++)
        {
            // End of STS found: Look for LTS peaks
            if(input[x].tag == STS_END)
            {
                // Cross correlate against the LTS
                std::vector<std::pair<double, int> > peaks;
                for(int p = x; p < x + CARRYOVER_LENGTH - LTS_LENGTH; p++)
                {
                    std::complex<double> corr(0, 0);
                    double power = 0;
                    for(int s = 0; s < 64; s++)
                    {
                        corr += input[p+s].sample * LTS_TIME_DOMAIN_CONJ[s] /* complex conjugate of LTS */;
                        power += std::norm(input[p+s].sample);
                    }
                    double corr_norm = std::abs(corr) / power;
                    if(corr_norm > LTS_CORR_THRESHOLD) peaks.push_back(std::pair<double, int>(corr_norm, p));
                }

                std::sort(peaks.begin(), peaks.end());
                std::reverse(peaks.begin(), peaks.end());

                // Look for two peaks, 64 samples apart
                bool found = false;
                int jump = 5;
                for(int s = 0; s < std::min((int)peaks.size(), 3) && !found; s+=jump)
                {
                    for(int t = s; t < std::min((int)peaks.size(), s+jump) && !found; t++)
                    {
                        if(std::abs(peaks[s].second - peaks[t].second) == 64)
                        {
                            // Determine the LTS offset
                            found = true;
                            int lts_offset = std::min(peaks[s].second, peaks[t].second) - 32; // Start of the LTS CP
                            if(lts_offset < 0) break;

                            input[lts_offset+24].tag = LTS1; // First sample in the LTS
                            input[lts_offset+24+64].tag = LTS2; // First sample in the LTS

                            std::complex<double> auto_corr_acc(0.0, 0.0);
                            for(int k = LTS1; k < LTS1; k++)
                            {
                                auto_corr_acc += input[k].sample * std::conj(input[k+LTS_LENGTH].sample);
                            }

                            m_phase_offset = std::arg(auto_corr_acc) / 64.0;
                            m_phase_acc = std::arg(input[lts_offset + 32 + LTS_LENGTH*2 -1].sample * LTS_TIME_DOMAIN_CONJ[63]);
                        }
                    }
                }
            }

            m_phase_acc += m_phase_offset;
            while(m_phase_acc > 2.0*M_PI) m_phase_acc -= 2.0*M_PI;
            while(m_phase_acc < -2.0*M_PI) m_phase_acc += 2.0*M_PI;
            std::complex<double> phase_correction(std::cos(m_phase_acc), std::sin(m_phase_acc));
            input[x].sample *= phase_correction;

        }

        // Copy working samples to output
        memcpy(&output_buffer[0],
               &input[0],
               input_buffer.size() * sizeof(tagged_sample));

        // Carryover last 160 samples from input buffer
        memcpy(&m_carryover[0],
               &input[input_buffer.size()],
               CARRYOVER_LENGTH * sizeof(tagged_sample));

    }


}
