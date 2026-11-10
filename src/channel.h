/*! \file channel.h
 * \brief Header file for the channel model that sim sends frames through.
 *
 * The channel does to the samples what the air and two radios do between
 * tx and rx, in this order:
 *
 * ~~~
 * frame → multipath → timing offset → frequency offset → + noise → receiver
 * ~~~
 *
 * - **Multipath:** echoes. Each frame goes through its own random channel,
 *   from 802.11's exponential model (see channel_params::delay_spread_ns).
 * - **Timing offset:** a delay, which can be a fraction of a sample, so the
 *   receiver samples the signal between the transmitter's samples.
 * - **Carrier frequency offset:** the radios' oscillators disagree, so the
 *   signal rotates by a little more on every sample.
 * - **Noise:** complex white Gaussian noise (AWGN) at a set SNR.
 *
 * Each is off by default, which makes the channel perfect: it passes the
 * samples through unchanged.
 */

#ifndef CHANNEL_H
#define CHANNEL_H

#include <complex>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

namespace fun {
/*!
 * \brief The channel's impairments. Each is off by default.
 */
struct channel_params {
  /*!
   * SNR in dB: the signal power over the noise power, or std::nullopt for no
   * noise. The signal power is the first frame's mean power, unless
   * channel::set_signal_power() sets it first. The noise floor then stays
   * where it is, and every frame has about the same power, so later frames
   * see about the same SNR.
   */
  std::optional<double> snr_db;

  //! Carrier frequency offset in Hz: the receiver's oscillator minus the
  //! transmitter's
  double cfo_hz = 0;

  /*!
   * Delay in samples, which can have a fractional part. A fractional delay
   * interpolates the signal with a windowed sinc filter, which adds
   * channel::INTERPOLATION_LATENCY samples to the delay.
   */
  double timing_offset = 0;

  /*!
   * RMS delay spread in ns, or 0 for no multipath. Each frame goes through
   * its own random channel from 802.11's exponential model (Chayat's model, in
   * IEEE 802.11-97/96): tap k, at k samples, is complex Gaussian with mean
   * power proportional to exp(-k T / delay spread), where T is the sample
   * period, for k from 0 to 10 delay spreads. The tap powers add up to 1, so
   * on average the channel neither amplifies nor attenuates. Any one frame's
   * channel can be a deep fade, though: that's Rayleigh fading.
   */
  double delay_spread_ns = 0;
};

/*!
 * \brief Sends samples through a channel with the impairments in a
 * channel_params.
 *
 * It's a stream: what one call can't fit in its output, like the tail of a
 * frame's echoes or a delay, comes out at the start of the next call's.
 * The frequency offset's phase carries on from call to call too.
 *
 * Everything random comes from std::mt19937_64, and the Gaussians from the
 * Box-Muller transform of its output. The standard defines the generator
 * exactly, unlike std::normal_distribution, so a seed gives the same channel
 * with any compiler and on any machine, up to rounding in the math library.
 */
class channel {
 public:
  //! Half the length of the fractional delay's interpolation filter, which is
  //! also the delay it adds
  static constexpr std::size_t INTERPOLATION_LATENCY = 16;

  /*!
   * \brief Constructor for channel.
   * \param params The impairments
   * \param sample_rate The sample rate in Hz, which converts
   * channel_params::cfo_hz and channel_params::delay_spread_ns to samples
   * \param seed The seed for the noise and the multipath. A random_source
   * with the same seed gives unrelated numbers.
   */
  channel(const channel_params& params, double sample_rate, std::uint64_t seed);

  /*!
   * \brief Sets the signal power that channel_params::snr_db is relative to,
   * and so the noise floor, instead of taking the first frame's power.
   * \param power The mean power of a sample
   *
   * Without it, there's no noise until the first frame, so a channel that
   * starts idle, or never sends a frame, needs it.
   */
  void set_signal_power(double power);

  /*!
   * \brief Sends a frame through the channel.
   * \param frame The frame's samples
   * \return As many samples as \p frame: the start of what the receiver gets
   * for it, after the end of what earlier calls sent.
   */
  std::vector<std::complex<double>> transmit(
      const std::vector<std::complex<double>>& frame);

  /*!
   * \brief Runs the channel while nothing is sent.
   * \param size The number of samples
   * \return \p size samples: the rest of what earlier calls sent, then noise.
   * There's no noise before the noise floor is set, by the first frame or
   * set_signal_power().
   */
  std::vector<std::complex<double>> idle(std::size_t size);

 private:
  /*!
   * \brief A complex Gaussian with a mean power of 1.
   */
  std::complex<double> gaussian();

  /*!
   * \brief Sends samples through the channel, with a frame's impulse
   * response.
   * \param samples The samples
   * \param response The impulse response: one frame's multipath and the delay
   * \return As many samples as \p samples.
   */
  std::vector<std::complex<double>> run(
      const std::vector<std::complex<double>>& samples,
      const std::vector<std::complex<double>>& response);

  channel_params m_params;  //!< The impairments

  //! The delay's impulse response: an impulse, or an interpolation filter
  std::vector<double> m_delay;

  //! Each multipath tap's mean power, or empty for no multipath
  std::vector<double> m_tap_powers;

  //! The frequency offset's phase change per sample, in radians
  double m_phase_step;

  double m_phase = 0;  //!< The frequency offset's phase, in [-π, π]

  //! The noise power, once the first frame has set it
  std::optional<double> m_noise_power;

  //! What earlier calls sent that's still to come out
  std::vector<std::complex<double>> m_pending;

  std::mt19937_64 m_random;  //!< Random number generator
};
}  // namespace fun

#endif  // CHANNEL_H
