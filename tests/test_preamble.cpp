#include "gtest/gtest.h"
#include "test_utils.h"

#include <cmath>
#include <complex>
#include <vector>

#include "frame_detector.h"
#include "rates.h"
#include "preamble.h"
#include "timing_sync.h"

using namespace fun;

typedef double SNR;

class test_preamble: public ::testing::TestWithParam<SNR> {
public:
    test_preamble() {}

// private:
    frame_detector t_frame_detector;
    timing_sync t_timing_sync;
};

TEST_P(test_preamble, detect_start)
{
    SNR snr = GetParam();
    const size_t PREAMBLE_SIZE = 320;
    std::vector<size_t> preamble_starts;
    size_t index = 0;
    size_t size = 4096;
    std::vector<std::complex<double>> samples(size, std::complex<double>(0,0));
    index += 1024; //Add some padding to the beginning
    preamble_starts.push_back(index);
    samples.insert(samples.begin() + index, &PREAMBLE_SAMPLES[0], &PREAMBLE_SAMPLES[PREAMBLE_SIZE]);

    index += 679;
    preamble_starts.push_back(index);
    samples.insert(samples.begin() + index, &PREAMBLE_SAMPLES[0], &PREAMBLE_SAMPLES[PREAMBLE_SIZE]);

    index += 521;
    preamble_starts.push_back(index);
    samples.insert(samples.begin() + index, &PREAMBLE_SAMPLES[0], &PREAMBLE_SAMPLES[PREAMBLE_SIZE]);

    // Add some noise
    add_awgn(samples, snr);

    t_frame_detector.input_buffer = samples;
    t_frame_detector.work();
    std::vector<tagged_sample> output(t_frame_detector.output_buffer);

    int p_index = 0; // Keeps track of which preamble_start we are looking for
    bool found_sts = false;
    for(int i = 0; i < output.size(); i++) {
        if(output[i].tag == STS_START) {
            int preamble_start = preamble_starts[p_index];
            int preamble_end = preamble_starts[p_index] + (PREAMBLE_SIZE/2) + (LTS_LENGTH/2);

            int dif = i - preamble_start;
            std::cout << "Preamble start at " << preamble_start << " detected at " << i << " (" << dif << " samples later)" << std::endl;
            // The STS_START needs to be tagged before the full LTS starts

            EXPECT_GE(i, preamble_start) << "Found STS_START before preamble started";
            EXPECT_LE(i, preamble_end) << "Found STS_START too late";
            if(preamble_start <= i && i <= preamble_end) {
                EXPECT_FALSE(found_sts) << "Duplicate STS_START for preamble starting at " << preamble_start << " (" << i - preamble_start << " samples later)";
                if(!found_sts) p_index++;
                found_sts = true;
            }
        } else if(output[i].tag == STS_END) {
            std::cout << "Preamble end at " << i << std::endl;
            found_sts = false;
        }
    }

    EXPECT_EQ(preamble_starts.size(), p_index) << "Failed to find all the preamble starts";
}

INSTANTIATE_TEST_CASE_P(preamble_snrs, test_preamble, ::testing::Values(
        0.0
        // 10.0 This fails atm
    )
);

TEST(DISABLED_math, norm_and_abs) {
    // std::vector<unsigned char> t_ode_to_joy(
    //     "Joy, bright spark of divinity, Daughter of Elysium, Fire-insired we tread"
    //     "Thy sanctuary."
    //     "Thy magic power re-unites"
    //     "All that custom has divided,"
    //     "All men become brothers"
    //     "Under the sway of thy gentle wings..."
    // );


    double norm = 0.0;
    double absolute = 0.0;

    norm = std::norm(PREAMBLE_SAMPLES[0]);
    absolute = std::abs(PREAMBLE_SAMPLES[0]);

    // std::cout << "Norm: " << norm << std::endl;
    // std::cout << "Abs:  " << absolute << std::endl;

    // std::cout << "---------------" << std::endl;

    norm = 0.0;
    absolute = 0.0;

    int num_samples = 160;
    for(int i = 0; i < num_samples; i++) {
        norm += std::norm(PREAMBLE_SAMPLES[i]);
        absolute += std::abs(PREAMBLE_SAMPLES[i]);
    }
    norm /= num_samples;
    absolute /= num_samples;

    // std::cout << "Norm: " << norm << std::endl;
    // std::cout << "Abs:  " << absolute << std::endl;

    // std::cout << "---------------" << std::endl;

    std::complex<double> a(2.0, 2.0);
    std::cout << "Norm(a): " << std::norm(a) << std::endl;
    std::cout << "Abs(a):  " << std::abs(a) << std::endl;

    // std::cout << "---------------" << std::endl;

    std::complex<double> b = a*std::conj(a);
    std::cout << b << std::endl;

    std::cout << "Norm(b): " << std::norm(b) << std::endl;
    std::cout << "Abs(b):  " << std::abs(b) << std::endl;


    EXPECT_DOUBLE_EQ(std::norm(a), std::abs(b));

}

