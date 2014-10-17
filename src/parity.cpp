/*! \file parity.cpp
 *  \brief C++ file for parity.
 *
 *  The parity class is used to compute the 1 bit parity in the PHY header.
 */

#include "parity.h"

namespace fun
{
    unsigned char Partab[256];
    int Parity_initialized;

    /* Create 256-entry odd-parity lookup table
     * Needed only on non-ia32 machines
     */
    void partab_init(void){
      int i,cnt,ti;

      /* Initialize parity lookup table */
      for(i=0;i<256;i++){
        cnt = 0;
        ti = i;
        while(ti){
          if(ti & 1)
            cnt++;
          ti >>= 1;
        }
        Partab[i] = cnt & 1;
      }
      Parity_initialized=1;
    }
}



