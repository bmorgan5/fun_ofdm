/*! \file host_info.cpp
 * \brief C++ file for what fun-ofdm-bench records about the machine it runs
 * on.
 */

#include "host_info.h"

#include <sched.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>

namespace fun {
/*!
 * \brief A file's whole text, or "" if it can't be read.
 */
static std::string read_file(const std::string& path) {
  std::ifstream file(path);
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

/*!
 * \brief The value of the first "key : value" line in /proc/cpuinfo's text
 * with this key, or "" if there's none.
 */
static std::string cpuinfo_value(const std::string& cpuinfo,
                                 const std::string& key) {
  std::istringstream lines(cpuinfo);
  for (std::string line; std::getline(lines, line);) {
    std::size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string name = line.substr(0, colon);
    name.erase(name.find_last_not_of(" \t") + 1);
    if (name != key) continue;
    std::size_t start = line.find_first_not_of(" \t", colon + 1);
    return start == std::string::npos ? "" : line.substr(start);
  }
  return "";
}

/*!
 * The ARM cores the project is likely to run on: the Pi 5's Cortex-A76, older
 * Pis' A72 and A53, and the Neoverse cores of cloud machines like GitHub's
 * arm runners.
 */
std::string cpu_model_from_cpuinfo(const std::string& cpuinfo) {
  std::string model = cpuinfo_value(cpuinfo, "model name");
  if (!model.empty()) return model;

  std::string implementer = cpuinfo_value(cpuinfo, "CPU implementer");
  std::string part = cpuinfo_value(cpuinfo, "CPU part");
  if (implementer.empty() || part.empty()) return "";

  static const std::map<std::string, std::string> ARM_CORES = {
      {"0xd03", "Cortex-A53"},  {"0xd05", "Cortex-A55"},
      {"0xd08", "Cortex-A72"},  {"0xd0b", "Cortex-A76"},
      {"0xd0c", "Neoverse-N1"}, {"0xd40", "Neoverse-V1"},
      {"0xd49", "Neoverse-N2"}, {"0xd4f", "Neoverse-V2"},
  };
  auto core = ARM_CORES.find(part);
  if (implementer == "0x41" && core != ARM_CORES.end())
    return "ARM " + core->second;
  return "CPU implementer " + implementer + ", part " + part;
}

std::optional<double> cpu_quota_from_cpu_max(const std::string& cpu_max) {
  std::istringstream fields(cpu_max);
  std::string quota;
  double period = 0;
  if (!(fields >> quota >> period) || quota == "max" || period <= 0)
    return std::nullopt;
  try {
    return std::stod(quota) / period;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

/*!
 * \brief The process's CPU quota: the smallest of its cgroup's and every
 * parent's, since each limits everything below it.
 *
 * /proc/self/cgroup's "0::<path>" line names the process's cgroup v2 group,
 * under /sys/fs/cgroup. In a container, that's usually "/", the container's
 * own root, which holds the limit Docker sets.
 */
static std::optional<double> read_cpu_quota(const std::string& root) {
  std::string cgroup;
  std::istringstream lines(read_file(root + "/proc/self/cgroup"));
  for (std::string line; std::getline(lines, line);) {
    if (line.starts_with("0::")) cgroup = line.substr(3);
  }
  if (cgroup.empty()) return std::nullopt;

  std::optional<double> quota;
  for (std::filesystem::path group = cgroup;; group = group.parent_path()) {
    std::optional<double> limit = cpu_quota_from_cpu_max(
        read_file(root + "/sys/fs/cgroup" + group.string() + "/cpu.max"));
    if (limit && (!quota || *limit < *quota)) quota = limit;
    if (group == group.parent_path()) break;
  }
  return quota;
}

host_info read_host_info(const std::string& root) {
  host_info host;
  host.cpu_model = cpu_model_from_cpuinfo(read_file(root + "/proc/cpuinfo"));

  // The device tree's strings end in a NUL
  host.board = read_file(root + "/sys/firmware/devicetree/base/model");
  host.board.erase(std::find(host.board.begin(), host.board.end(), '\0'),
                   host.board.end());

  utsname names;
  if (uname(&names) == 0) {
    host.arch = names.machine;
    host.kernel = names.release;
  }

  host.online_cpus = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
  cpu_set_t affinity;
  if (sched_getaffinity(0, sizeof(affinity), &affinity) == 0)
    host.usable_cpus = CPU_COUNT(&affinity);
  host.cpu_quota = read_cpu_quota(root);

  if (std::filesystem::exists(root + "/.dockerenv"))
    host.container = "docker";
  else if (std::filesystem::exists(root + "/run/.containerenv"))
    host.container = "podman";
  return host;
}

std::string cpu_limit_warning(const host_info& host) {
  std::string limits;
  int usable = host.online_cpus;
  if (host.usable_cpus && host.usable_cpus < usable) {
    usable = host.usable_cpus;
    limits = "CPU affinity, like Docker's --cpuset-cpus";
  }
  bool quota_limits = host.cpu_quota && *host.cpu_quota < usable;
  if (quota_limits) {
    if (!limits.empty()) limits += ", and ";
    limits += "a CPU quota, like Docker's --cpus";
  }
  if (limits.empty()) return "";

  char amount[32];
  if (quota_limits)
    std::snprintf(amount, sizeof(amount), "%g", *host.cpu_quota);
  else
    std::snprintf(amount, sizeof(amount), "%d", usable);
  return "This process may use only " + std::string(amount) + " of the " +
         std::to_string(host.online_cpus) + " CPUs (" + limits + ")";
}
}  // namespace fun
