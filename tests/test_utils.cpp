#include "test_utils.h"

#include <cmath>
#include <functional>
#include <random>

double signal_power(std::vector<std::complex<double>> &signal) {
    if(signal.size() <= 0) return 0.0;

    double power = 0.0;
    for(int i = 0; i < signal.size(); i++) {
        power += std::norm(signal[i]);
    }
    return power / (double)signal.size(); 
}

// 0 SNR means no noise
void add_awgn(std::vector<std::complex<double>> &signal, double SNR) {
    if(SNR == 0.0) return;

    double sig_pwr = signal_power(signal);
    double noise_std_dev = std::sqrt(sig_pwr) / SNR;
    auto dist = std::bind(std::normal_distribution<double>{0.0, noise_std_dev}, std::default_random_engine{});
    for(size_t i = 0; i < signal.size(); i++) {
        signal[i] += std::complex<double>(dist(), dist());
    }
}