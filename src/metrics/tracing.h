/*! \file tracing.h
 * \brief Header file for the receiver's traces, with the Perfetto SDK.
 *
 * A trace records each event with its time and thread, in order, unlike a
 * histogram, so it shows bursts and backlogs. It's off unless a session is
 * started, and then each event costs a few hundred nanoseconds; without one,
 * a check of a flag. Traces open in https://ui.perfetto.dev.
 *
 * There are two kinds of session:
 * - **A file** (start_file()): this process records its own events into a
 *   file.
 * - **The system's** (start_system()): the events go to Perfetto's traced
 *   service, which records them with whatever else its session asks for,
 *   like the kernel's scheduler events. The `perfetto` command starts the
 *   session and writes the file.
 *
 * In a build without metrics (metrics.h), every function does nothing.
 */

#ifndef METRICS_TRACING_H
#define METRICS_TRACING_H

#include <cstdint>
#include <initializer_list>
#include <string>

#include "metrics.h"

namespace fun::trace {
//! The category of every event, which a session's config enables
constexpr const char* CATEGORY = "fun_ofdm";

//! A number to record with an event, shown when the event is selected
struct arg {
  const char* name;    //!< The argument's name, a string literal
  std::int64_t value;  //!< Its value
};

/*!
 * \brief Starts recording this process's events into a file, which is written
 * as the trace goes, so it can be as long as the disk allows.
 * \param path The file
 * \throws std::runtime_error if the file can't be opened or the session
 * doesn't start.
 */
void start_file(const std::string& path);

/*!
 * \brief Connects to the system's traced service, so that its sessions record
 * this process's events. Nothing is recorded until a session that enables
 * #CATEGORY starts.
 */
void start_system();

/*!
 * \brief Ends the file's session, if there is one, and writes the rest of the
 * file.
 */
void stop();

/*!
 * \brief Names the calling thread, in traces and in tools like top and perf.
 * \param name The name. The kernel keeps only its first 15 characters.
 */
void name_thread(const std::string& name);

/*!
 * \brief Records a slice of time on the calling thread.
 * \param name The slice's name, which must outlive the trace, like a string
 * literal
 * \param start When it started
 * \param end When it ended
 * \param args Numbers to record with it
 */
void slice(const char* name, metrics::time_point start, metrics::time_point end,
           std::initializer_list<arg> args = {});

/*!
 * \brief Records an instant on the calling thread.
 * \param name The event's name, which must outlive the trace
 * \param at When it happened
 * \param args Numbers to record with it
 */
void instant(const char* name, metrics::time_point at,
             std::initializer_list<arg> args = {});
}  // namespace fun::trace

#endif  // METRICS_TRACING_H
