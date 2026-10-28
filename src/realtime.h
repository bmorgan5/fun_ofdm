/*! \file realtime.h
 *  \brief Header file for setting real time thread priority.
 */

#ifndef REALTIME_H
#define REALTIME_H

namespace fun
{
    /*!
     * \brief Attempt to set real time priority for thread scheduling
     * \return Whether or not real time priority was succesfully set.
     */
    bool set_realtime_priority();
}

#endif // REALTIME_H
