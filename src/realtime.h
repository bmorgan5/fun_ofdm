/*! \file realtime.h
 * \brief Asks the OS to schedule the calling thread in real time.
 *
 * The receiver has to read samples from the USRP as fast as they arrive, or
 * the device overflows and drops them. A real time thread isn't preempted by
 * ordinary ones, so it keeps up better on a busy machine.
 */

#ifndef REALTIME_H
#define REALTIME_H

namespace fun {
/*!
 * \brief Sets the calling thread's scheduling policy to SCHED_RR, at the
 * highest priority.
 * \return Whether it was set. It fails without root (or CAP_SYS_NICE), and then
 * prints a message saying so.
 *
 * Only the calling thread changes, but threads it starts afterwards inherit
 * the policy, so call it before creating the objects that start threads, like
 * receiver.
 */
bool set_realtime_priority();
}  // namespace fun

#endif  // REALTIME_H
