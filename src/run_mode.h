/*! \file run_mode.h
 * \brief Header file for running a mode and writing its run summary.
 *
 * run_mode() runs one of fun-ofdm's modes, and writes its summary to its
 * --json file as soon as the mode reports its result, before the mode closes
 * its USRP.
 */

#ifndef RUN_MODE_H
#define RUN_MODE_H

#include <concepts>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <stop_token>
#include <streambuf>
#include <string>

#include "src/metrics/tracing.h"
#include "summary.h"

namespace fun {
/*!
 * \brief What a mode calls with its result, once the result is complete, to
 * write its summary.
 */
template <typename Result>
using reporter = std::function<void(const Result&)>;

/*!
 * \brief A mode: it runs until it's done or stopped, reports its result, and
 * returns it.
 */
template <typename Options, typename Result>
using mode = Result (*)(const Options&, std::stop_token,
                        const reporter<Result>&);

/*!
 * \brief The process exit code for a tx run: 0.
 */
inline int exit_code(const tx_result&) { return 0; }

/*!
 * \brief The process exit code for an rx run: 0, since it only stops when it
 * is asked to.
 */
inline int exit_code(const rx_result&) { return 0; }

/*!
 * \brief The process exit code for a sim or loopback run: 0 if all the data
 * was received intact or the run was stopped, otherwise 1.
 */
template <typename Result>
  requires std::same_as<Result, sim_result> ||
           std::same_as<Result, loopback_result>
int exit_code(const Result& result) {
  return !result.check || result.check->passed() ? 0 : 1;
}

/*!
 * \brief Sends what's written to std::cout to stderr instead, until it's
 * destroyed.
 */
class stdout_to_stderr {
 public:
  stdout_to_stderr() : m_stdout(std::cout.rdbuf(std::cerr.rdbuf())) {}
  ~stdout_to_stderr() { std::cout.rdbuf(m_stdout); }
  stdout_to_stderr(const stdout_to_stderr&) = delete;
  stdout_to_stderr& operator=(const stdout_to_stderr&) = delete;

  /*!
   * \brief stdout's own buffer, which still writes to stdout.
   */
  std::streambuf* stdout_buffer() const { return m_stdout; }

 private:
  std::streambuf* m_stdout;  //!< std::cout's buffer, to put back
};

/*!
 * \brief Runs a mode, and writes its run summary to its --json file, if it has
 * one.
 * \param run The mode
 * \param opts The mode's options
 * \param stop Stops the mode
 * \param version fun-ofdm's version, for the summary
 * \return The process exit code, from exit_code().
 * \throws std::runtime_error if the --json file can't be written.
 *
 * The file is opened before the mode runs, so a bad path fails at once, not
 * after a long run. It's written when the mode reports its result, or when the
 * mode returns if it doesn't, and only once. A mode with a USRP reports before
 * it closes the USRP, which can hang (see src/usrp/usrp.h), so the summary is
 * written even if the process is killed then. If the mode throws before it
 * reports, the file is left empty.
 *
 * For "-", the summary goes to stdout, and everything else the mode writes to
 * std::cout goes to stderr, so that stdout is only JSON.
 *
 * It also starts the mode's traces, if it has any, and stops them when it
 * ends.
 */
template <typename Options, typename Result>
int run_mode(mode<Options, Result> run, const Options& opts,
             std::stop_token stop, const std::string& version) {
  std::optional<stdout_to_stderr> redirect;
  std::ofstream json_file;
  if (opts.json_file == "-") {
    redirect.emplace();
  } else if (opts.json_file) {
    json_file.open(*opts.json_file);
    if (!json_file) throw std::runtime_error("Can't open " + *opts.json_file);
  }

  bool written = false;
  auto write = [&](const Result& result) {
    if (!opts.json_file || written) return;
    written = true;
    std::ostream out(json_file.is_open() ? json_file.rdbuf()
                                         : redirect->stdout_buffer());
    out << to_json(version, opts, result) << std::endl;
    if (!out) throw std::runtime_error("Can't write " + *opts.json_file);
  };

  // Traces end when the mode does, even if it throws
  struct trace_stopper {
    ~trace_stopper() { trace::stop(); }
  } stopper;
  if constexpr (requires { opts.trace; }) {
    if (opts.trace.system) trace::start_system();
    if (opts.trace.file) trace::start_file(*opts.trace.file);
  }

  Result result = run(opts, stop, write);
  write(result);
  return exit_code(result);
}
}  // namespace fun

#endif  // RUN_MODE_H
