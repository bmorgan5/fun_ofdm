/*! \file host_info.h
 * \brief Header file for what fun-ofdm-bench records about the machine it
 * runs on.
 *
 * google_benchmark prints the number of CPUs, their clock and caches, but
 * not the processor. Its CPU count is every CPU the kernel has online, even
 * in a container that may only use some of them: Docker's `--cpus` sets a
 * CPU quota, and `--cpuset-cpus` limits which CPUs the process may run on.
 * The receiver chain runs seven threads, so both change its throughput.
 *
 * This reads the rest from Linux's /proc and /sys files, and uname().
 */

#ifndef HOST_INFO_H
#define HOST_INFO_H

#include <optional>
#include <string>

namespace fun {
/*!
 * \brief The machine a benchmark runs on.
 */
struct host_info {
  //! The processor, e.g. "Intel(R) Core(TM) i7-10700K CPU @ 3.80GHz" or
  //! "ARM Cortex-A76", or "" if it's unknown
  std::string cpu_model;

  //! The board, from the device tree, e.g. "Raspberry Pi 5 Model B Rev 1.0",
  //! or "" if there isn't one, as on x86
  std::string board;

  std::string arch;    //!< The machine's architecture, e.g. "x86_64"
  std::string kernel;  //!< The kernel's release, e.g. "6.8.0-45-generic"

  int online_cpus = 0;  //!< The CPUs the kernel has online
  int usable_cpus = 0;  //!< The CPUs this process may run on (its affinity)

  //! The CPU time the process's cgroup may use, in CPUs (cgroup v2's
  //! cpu.max), or std::nullopt if it's not limited
  std::optional<double> cpu_quota;

  //! "docker", "podman", or "" if it doesn't run in a container it can
  //! recognize
  std::string container;
};

/*!
 * \brief Reads the machine's details.
 * \param root Prepended to every file's path, for the tests: "" for the real
 * files
 */
host_info read_host_info(const std::string& root = "");

/*!
 * \brief The processor's name, from the text of /proc/cpuinfo.
 * \return x86's "model name" for the first CPU. On ARM, which has none, the
 * core's name from its "CPU implementer" and "CPU part", like "ARM
 * Cortex-A76", or their numbers for a core this doesn't know. "" if there's
 * neither.
 */
std::string cpu_model_from_cpuinfo(const std::string& cpuinfo);

/*!
 * \brief The CPU quota, from the text of cgroup v2's cpu.max: "<quota>
 * <period>", both in µs, or "max <period>" for no limit.
 * \return The quota in CPUs, quota / period, or std::nullopt for no limit or
 * text this can't read.
 */
std::optional<double> cpu_quota_from_cpu_max(const std::string& cpu_max);

/*!
 * \brief Describes any limit on the CPUs the process can use.
 * \return E.g. "This process may use only 4 of the 16 CPUs (CPU affinity)",
 * or "" if it may use them all.
 */
std::string cpu_limit_warning(const host_info& host);
}  // namespace fun

#endif  // HOST_INFO_H
