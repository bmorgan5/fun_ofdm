/*! \file parity.h
 *  \brief Parity of a word: 1 if it has an odd number of 1 bits, else 0.
 *
 *  Two users:
 *  - The SIGNAL field ends with a parity bit chosen so its first 18 bits have
 *    an even number of 1s. The receiver checks it to reject most corrupted
 *    headers (see ppdu.h).
 *  - The convolutional encoder computes each coded bit as the parity of some
 *    of its shift register's bits (see viterbi.h).
 */

#ifndef PARITY_H
#define PARITY_H

namespace fun {

/**
 * static inline parity(int x) -- compute parity (up to 32 bits?)
 */

/* Determine parity of argument: 1 = odd, 0 = even */
#ifdef __i386__
static inline int parityb(unsigned char x) {
  __asm__ __volatile__("test %1,%1;setpo %0" : "=g"(x) : "r"(x));
  return x;
}
#else

#if __cplusplus
extern "C" {
#endif
void partab_init();
#if __cplusplus
};
#endif

static inline int parityb(unsigned char x) {
  extern unsigned char Partab[256];
  extern int Parity_initialized;
  if (!Parity_initialized) {
    partab_init();
  }
  return Partab[x];
}
#endif

static inline int parity(int x) {
  /* Fold down to one byte */
  x ^= (x >> 16);
  x ^= (x >> 8);
  return parityb(x);
}
}  // namespace fun

#endif  // PARITY_H
