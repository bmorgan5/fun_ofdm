#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <complex>
#include <vector>

double signal_power(std::vector<std::complex<double>> &signal);
void add_awgn(std::vector<std::complex<double>> &signal, double SNR);

#endif