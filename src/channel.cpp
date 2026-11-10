/*! \file channel.cpp
 * \brief C++ file for the channel model that sim sends frames through.
 */

#include "channel.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fun {
/*!
 * \brief The impulse response of a delay.
 * \param delay The delay in samples, at least 0
 * \return An impulse at \p delay if it's a whole number of samples. Otherwise
 * a sinc interpolation filter, with a Blackman window, centred at
 * channel::INTERPOLATION_LATENCY samples more than \p delay. Its taps add up
 * to 1, so it passes a constant through unchanged.
 */
static std::vector<double> delay_response(double delay) {
  const std::size_t K = channel::INTERPOLATION_LATENCY;
  auto whole = static_cast<std::size_t>(delay);
  double fraction = delay - static_cast<double>(whole);

  if (fraction == 0) {
    std::vector<double> response(whole + 1);
    response[whole] = 1;
    return response;
  }

  std::vector<double> response(whole + 2 * K + 1);
  double sum = 0;
  for (std::size_t n = 0; n <= 2 * K; n++) {
    // The tap's time from the filter's centre, in samples
    double t = static_cast<double>(n) - static_cast<double>(K) - fraction;
    double sinc = std::sin(std::numbers::pi * t) / (std::numbers::pi * t);
    // A Blackman window, which is 0 one sample past either end
    double x = std::numbers::pi * t / static_cast<double>(K + 1);
    double window = 0.42 + 0.5 * std::cos(x) + 0.08 * std::cos(2 * x);
    response[whole + n] = sinc * window;
    sum += sinc * window;
  }
  for (double& tap : response) tap /= sum;
  return response;
}

/*!
 * \brief The mean power of each tap of 802.11's exponential multipath model.
 * \param delay_spread The RMS delay spread in samples, or 0 for no multipath
 * \return The taps' powers, which add up to 1, or nothing for no multipath.
 */
static std::vector<double> tap_powers(double delay_spread) {
  if (delay_spread <= 0) return {};

  auto taps = static_cast<std::size_t>(std::ceil(10 * delay_spread)) + 1;
  std::vector<double> powers(taps);
  double sum = 0;
  for (std::size_t k = 0; k < taps; k++) {
    powers[k] = std::exp(-static_cast<double>(k) / delay_spread);
    sum += powers[k];
  }
  for (double& power : powers) power /= sum;
  return powers;
}

/*!
 * The generator is seeded through a std::seed_seq with a constant added, so
 * that it doesn't give the same numbers as a random_source with the same seed.
 */
channel::channel(const channel_params& params, double sample_rate,
                 std::uint64_t seed)
    : m_params(params),
      m_delay(delay_response(params.timing_offset)),
      m_tap_powers(tap_powers(params.delay_spread_ns * 1e-9 * sample_rate)),
      m_phase_step(2 * std::numbers::pi * params.cfo_hz / sample_rate) {
  // "chan", so the sequence differs from just the seed
  const std::uint32_t STREAM = 0x6368616e;
  std::seed_seq sequence{static_cast<std::uint32_t>(seed),
                         static_cast<std::uint32_t>(seed >> 32), STREAM};
  m_random.seed(sequence);
}

std::vector<std::complex<double>> channel::transmit(
    const std::vector<std::complex<double>>& frame) {
  if (m_params.snr_db && !m_noise_power && !frame.empty()) {
    double power = 0;
    for (const auto& sample : frame) power += std::norm(sample);
    power /= static_cast<double>(frame.size());
    m_noise_power = power / std::pow(10, *m_params.snr_db / 10);
  }

  if (m_tap_powers.empty()) return run(frame, {m_delay.begin(), m_delay.end()});

  // This frame's multipath, then the delay
  std::vector<std::complex<double>> response(m_tap_powers.size() +
                                             m_delay.size() - 1);
  for (std::size_t k = 0; k < m_tap_powers.size(); k++) {
    std::complex<double> tap = std::sqrt(m_tap_powers[k]) * gaussian();
    for (std::size_t d = 0; d < m_delay.size(); d++)
      response[k + d] += tap * m_delay[d];
  }
  return run(frame, response);
}

std::vector<std::complex<double>> channel::idle(std::size_t size) {
  return run(std::vector<std::complex<double>>(size), {});
}

/*!
 * Box-Muller: for u1 in (0, 1] and u2 in [0, 1), sqrt(-ln u1) is
 * Rayleigh-distributed with a mean square of 1, and 2π u2 a uniform phase.
 * Each uniform takes the top 53 bits of one of the generator's numbers.
 */
std::complex<double> channel::gaussian() {
  const double SCALE = 0x1.0p-53;
  double u1 = 1 - static_cast<double>(m_random() >> 11) * SCALE;
  double u2 = static_cast<double>(m_random() >> 11) * SCALE;
  return std::polar(std::sqrt(-std::log(u1)), 2 * std::numbers::pi * u2);
}

/*!
 * #m_pending holds what's still to come out, starting at this call's first
 * sample. This adds the samples, convolved with \p response, to it, takes as
 * many samples as came in off its front, and rotates and adds noise to them.
 * An empty \p response adds nothing, for idle().
 */
std::vector<std::complex<double>> channel::run(
    const std::vector<std::complex<double>>& samples,
    const std::vector<std::complex<double>>& response) {
  std::size_t size = samples.size();
  std::size_t needed = size;
  if (!response.empty() && size) needed = size + response.size() - 1;
  if (m_pending.size() < needed) m_pending.resize(needed);

  if (!response.empty()) {
    for (std::size_t i = 0; i < size; i++) {
      for (std::size_t k = 0; k < response.size(); k++)
        m_pending[i + k] += samples[i] * response[k];
    }
  }

  std::vector<std::complex<double>> out(m_pending.begin(),
                                        m_pending.begin() + size);
  m_pending.erase(m_pending.begin(), m_pending.begin() + size);

  if (m_phase_step != 0) {
    for (auto& sample : out) {
      sample *= std::polar(1.0, m_phase);
      m_phase = std::remainder(m_phase + m_phase_step, 2 * std::numbers::pi);
    }
  }
  if (m_noise_power) {
    double amplitude = std::sqrt(*m_noise_power);
    for (auto& sample : out) sample += amplitude * gaussian();
  }
  return out;
}
}  // namespace fun
