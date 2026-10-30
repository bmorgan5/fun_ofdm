/*! \file phase_tracker.h
 *  \brief Header file for the Phase Tracker block.
 *
 *  The phase tracker block is in charge of tracking and correcting
 *  phase rotation accross symbols in a frame using the 4 pilot subcarriers.
 *  It also removes the pilot and null subcarriers passing on only the data
 *  subcarriers after any necessary frequency corrections have been made.
 */

#ifndef PHASE_TRACKER_H
#define PHASE_TRACKER_H

#include <complex>
#include <vector>

#include "block.h"
#include "tagged_vector.h"

namespace fun {
/*!
 * \brief The phase_tracker block.
 *
 * Inputs tagged_vector<64> from channel_est block.
 * Outputs tagged_vector <48> to frame_decoder block.
 *
 *  The phase tracker block is in charge of tracking and correcting
 *  phase rotation accross symbols in a frame using the 4 pilot subcarriers.
 *  It also removes the pilot and null subcarriers passing on only the data
 *  subcarriers after any necessary frequency corrections have been made.
 */
class phase_tracker : public fun::block<tagged_vector<64>, tagged_vector<48>> {
 public:
  phase_tracker();  //!< Constructor for phase_tracker block.

  virtual void work();  //!< Signal processing happens here.

 private:
  /*!
   * \brief Counter used to keep track of the symbol number in the frame so
   * as to know what pilot polarity to expect. This is reset at the beginning
   * of each new frame.
   */
  int m_symbol_count;
};
}  // namespace fun

#endif  // PHASE_TRACKER_H
