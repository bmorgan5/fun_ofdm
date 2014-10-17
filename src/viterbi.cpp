/*! \file viterbi.cpp
 *  \brief C++ file for the viterbi class.

*  The viterbi class contains methods for convolutionally encoding and decoding data
*  using the viterbi algorithm.
*/

#include "viterbi.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <memory.h>
#include <sys/resource.h>
#include <pmmintrin.h>
#include <emmintrin.h>
#include <xmmintrin.h>
#include <mmintrin.h>

#include <unistd.h>

#include "parity.h"

namespace fun
{

    /*!
     *  Main decode function.
     */
    void viterbi::conv_decode(unsigned char * symbols, unsigned char * data, int data_bits)
    {
      struct v * vp = viterbi_alloc(data_bits);
      viterbi_init(vp, 0);
      viterbi_decode(vp, &symbols[0], &data[0], data_bits);
      viterbi_free(vp);
    }

    void viterbi::conv_encode(unsigned char * data, unsigned char * symbols, int data_bits)
    {
        int symbol_count = RATE * (data_bits + 6);
        memset(symbols, 0, symbol_count);

        int polys[RATE] = POLYS;
        int sr = 0;

        int index = 0;
        for(int i = 0; i < data_bits+(K-1); i++) {

            int b = data[i/8];
            int j = i % 8;
            int bit = (b >> (7 - j)) & 1;

            sr = (sr << 1) | bit;
            for(int k = 0; k < RATE; k++)
            {
              int m = sr & polys[k];
              int par = parity(sr & polys[k]);
              symbols[index++] = par;
            }
        }
    }

    /* Initialize Viterbi decoder for start of new frame */

    /*!
     * \brief viterbi::viterbi_init
     * \param vp
     * \param starting_state
     */
    void viterbi::viterbi_init(struct v *vp, int starting_state) {
      for(int i=0; i < NUMSTATES; i++)
        vp->metrics1.t[i] = 63;

      vp->old_metrics = &vp->metrics1;
      vp->new_metrics = &vp->metrics2;
      vp->old_metrics->t[starting_state & (NUMSTATES-1)] = 0; /* Bias known start state */
    }

    /* Create a new instance of a Viterbi decoder */
    struct v * viterbi::viterbi_alloc(int len) {
      struct v *vp;
      static int Init = 0;

        int state, i;
        int polys[RATE] = POLYS;
        for (state=0;state < NUMSTATES/2;state++) {
          for (i=0; i<RATE; i++) {
            Branchtab[i*NUMSTATES/2+state] = (polys[i] < 0) ^ parity((2*state) & abs(polys[i])) ? 255 : 0;
          }
        }
        Init++;

      if (posix_memalign((void**)&vp, 16,sizeof(struct v)))
        return NULL;

      // NOTE: a frame-worth of decisions!
      if (posix_memalign((void**)&vp->decisions, 16,(len+(K-1))*sizeof(decision_t))) {
        free(vp);
        return NULL;
      }
      viterbi_init(vp, 0);

      return vp;
    }

    /* Viterbi chainback */
    void viterbi::viterbi_chainback(struct v *vp,
          unsigned char *data, /* Decoded output data */
          unsigned int nbits, /* Number of data bits */
          unsigned int endstate)  /* Terminal encoder state */
    {

      decision_t *d = vp->decisions;

    /* ADDSHIFT and SUBSHIFT make sure that the thing returned is a byte. */
    #if (K-1<8)
    #define ADDSHIFT (8-(K-1))
    #define SUBSHIFT 0
    #elif (K-1>8)
    #define ADDSHIFT 0
    #define SUBSHIFT ((K-1)-8)
    #else
    #define ADDSHIFT 0
    #define SUBSHIFT 0
    #endif

      /* Make room beyond the end of the encoder register so we can
       * accumulate a full byte of decoded data
       */
      endstate = (endstate % NUMSTATES) << ADDSHIFT;

      /* The store into data[] only needs to be done every 8 bits.
       * But this avoids a conditional branch, and the writes will
       * combine in the cache anyway
       */
      d += (K-1); /* Look past tail */
      while (nbits-- != 0) {
        int k = (d[nbits].w[(endstate >> ADDSHIFT)/32] >> ((endstate >> ADDSHIFT)%32)) & 1;
        endstate = (endstate >> 1) | (k << (K-2+ADDSHIFT));
        data[nbits >> 3] = endstate >> SUBSHIFT;
      }

    #undef ADDSHIRT
    #undef SUBSHIFT
    }

    /*!
     * \brief Delete instance of a Viterbi decoder
     * \param vp
     */
    void viterbi::viterbi_free(struct v *vp) {
      if (vp != NULL) {
        free(vp->decisions);
        free(vp);
      }
    }

    /*!
     * \brief viterbi::viterbi_decode
     * \param vp
     * \param symbols
     * \param data
     * \param nbits
     */
    void viterbi::viterbi_decode(struct v *vp, const COMPUTETYPE *symbols, unsigned char *data, int nbits) {
      // vp = viterbi decoder
      // data = decoded
      // symbols = signal

      /* Decode it and make sure we get the right answer */
      /* Initialize Viterbi decoder */
      viterbi_init(vp, 0);

      /* Decode block */
      //vp->update_blk(vp, symbols, nbits+(K-1));
      viterbi_update_blk_SPIRAL(vp, symbols, nbits + (K-1));

      /* Do Viterbi chainback */
      viterbi_chainback(vp, data, nbits, 0);
    }


    /*!
     * \brief viterbi::viterbi_update_blk_SPIRAL
     * \param vp
     * \param syms
     * \param nbits
     */
    void viterbi::viterbi_update_blk_SPIRAL(struct v *vp, const COMPUTETYPE *syms, int nbits) {
      decision_t *d = (decision_t *)vp->decisions;

      for (int s = 0; s < nbits; s++)
        memset(d+s, 0, sizeof(decision_t));

      FULL_SPIRAL(nbits, vp->new_metrics->t, vp->old_metrics->t, syms, d->t, Branchtab);
    }

    /*!
     * \brief viterbi::FULL_SPIRAL
     * \param nbits
     * \param Y
     * \param X
     * \param syms
     * \param dec
     * \param Branchtab
     */
    void viterbi::FULL_SPIRAL(int nbits, unsigned char *Y, unsigned char *X, const unsigned char *syms, unsigned char *dec, unsigned char *Branchtab) {
        for(int i9 = 0; i9 <= (nbits/2-1); i9++) {
            unsigned char a75, a81;
            int a73, a92;
            short int s20, s21, s26, s27;
            const unsigned char  *a74, *a80, *b6;
            short int  *a110, *a111, *a91, *a93, *a94;
            __m128i  *a102, *a112, *a113, *a71, *a72, *a77, *a83
                    , *a95, *a96, *a97, *a98, *a99;
            __m128i a105, a106, a86, a87;
            __m128i a100, a101, a103, a104, a107, a108, a109
                    , a76, a78, a79, a82, a84, a85, a88, a89
                    , a90, d10, d11, d12, d9, m23, m24, m25
                    , m26, m27, m28, m29, m30, s18, s19, s22
                    , s23, s24, s25, s28, s29, t13, t14, t15
                    , t16, t17, t18;
            a71 = ((__m128i  *) X);
            s18 = *(a71);
            a72 = (a71 + 2);
            s19 = *(a72);
            a73 = (4 * i9);
            a74 = (syms + a73);
            a75 = *(a74);
            a76 = _mm_set1_epi8(a75);
            a77 = ((__m128i  *) Branchtab);
            a78 = *(a77);
            a79 = _mm_xor_si128(a76, a78);
            b6 = (a73 + syms);
            a80 = (b6 + 1);
            a81 = *(a80);
            a82 = _mm_set1_epi8(a81);
            a83 = (a77 + 2);
            a84 = *(a83);
            a85 = _mm_xor_si128(a82, a84);
            t13 = _mm_avg_epu8(a79,a85);
            a86 = ((__m128i ) t13);
            a87 = _mm_srli_epi16(a86, 2);
            a88 = ((__m128i ) a87);
            t14 = _mm_and_si128(a88, _mm_set_epi8(63, 63, 63, 63, 63, 63, 63
        , 63, 63, 63, 63, 63, 63, 63, 63
        , 63));
            t15 = _mm_subs_epu8(_mm_set_epi8(63, 63, 63, 63, 63, 63, 63
        , 63, 63, 63, 63, 63, 63, 63, 63
        , 63), t14);
            m23 = _mm_adds_epu8(s18, t14);
            m24 = _mm_adds_epu8(s19, t15);
            m25 = _mm_adds_epu8(s18, t15);
            m26 = _mm_adds_epu8(s19, t14);
            a89 = _mm_min_epu8(m24, m23);
            d9 = _mm_cmpeq_epi8(a89, m24);
            a90 = _mm_min_epu8(m26, m25);
            d10 = _mm_cmpeq_epi8(a90, m26);
            s20 = _mm_movemask_epi8(_mm_unpacklo_epi8(d9,d10));
            a91 = ((short int  *) dec);
            a92 = (8 * i9);
            a93 = (a91 + a92);
            *(a93) = s20;
            s21 = _mm_movemask_epi8(_mm_unpackhi_epi8(d9,d10));
            a94 = (a93 + 1);
            *(a94) = s21;
            s22 = _mm_unpacklo_epi8(a89, a90);
            s23 = _mm_unpackhi_epi8(a89, a90);
            a95 = ((__m128i  *) Y);
            *(a95) = s22;
            a96 = (a95 + 1);
            *(a96) = s23;
            a97 = (a71 + 1);
            s24 = *(a97);
            a98 = (a71 + 3);
            s25 = *(a98);
            a99 = (a77 + 1);
            a100 = *(a99);
            a101 = _mm_xor_si128(a76, a100);
            a102 = (a77 + 3);
            a103 = *(a102);
            a104 = _mm_xor_si128(a82, a103);
            t16 = _mm_avg_epu8(a101,a104);
            a105 = ((__m128i ) t16);
            a106 = _mm_srli_epi16(a105, 2);
            a107 = ((__m128i ) a106);
            t17 = _mm_and_si128(a107, _mm_set_epi8(63, 63, 63, 63, 63, 63, 63
        , 63, 63, 63, 63, 63, 63, 63, 63
        , 63));
            t18 = _mm_subs_epu8(_mm_set_epi8(63, 63, 63, 63, 63, 63, 63
        , 63, 63, 63, 63, 63, 63, 63, 63
        , 63), t17);
            m27 = _mm_adds_epu8(s24, t17);
            m28 = _mm_adds_epu8(s25, t18);
            m29 = _mm_adds_epu8(s24, t18);
            m30 = _mm_adds_epu8(s25, t17);
            a108 = _mm_min_epu8(m28, m27);
            d11 = _mm_cmpeq_epi8(a108, m28);
            a109 = _mm_min_epu8(m30, m29);
            d12 = _mm_cmpeq_epi8(a109, m30);
            s26 = _mm_movemask_epi8(_mm_unpacklo_epi8(d11,d12));
            a110 = (a93 + 2);
            *(a110) = s26;
            s27 = _mm_movemask_epi8(_mm_unpackhi_epi8(d11,d12));
            a111 = (a93 + 3);
            *(a111) = s27;
            s28 = _mm_unpacklo_epi8(a108, a109);
            s29 = _mm_unpackhi_epi8(a108, a109);
            a112 = (a95 + 2);
            *(a112) = s28;
            a113 = (a95 + 3);
            *(a113) = s29;
            if ((((unsigned char  *) Y)[0]>210)) {
                __m128i m5, m6;
                m5 = ((__m128i  *) Y)[0];
                m5 = _mm_min_epu8(m5, ((__m128i  *) Y)[1]);
                m5 = _mm_min_epu8(m5, ((__m128i  *) Y)[2]);
                m5 = _mm_min_epu8(m5, ((__m128i  *) Y)[3]);
                __m128i m7;
                m7 = _mm_min_epu8(_mm_srli_si128(m5, 8), m5);
                m7 = ((__m128i ) _mm_min_epu8(((__m128i ) _mm_srli_epi64(m7, 32)), ((__m128i ) m7)));
                m7 = ((__m128i ) _mm_min_epu8(((__m128i ) _mm_srli_epi64(m7, 16)), ((__m128i ) m7)));
                m7 = ((__m128i ) _mm_min_epu8(((__m128i ) _mm_srli_epi64(m7, 8)), ((__m128i ) m7)));
                m7 = _mm_unpacklo_epi8(m7, m7);
                m7 = _mm_shufflelo_epi16(m7, _MM_SHUFFLE(0, 0, 0, 0));
                m6 = _mm_unpacklo_epi64(m7, m7);
                ((__m128i  *) Y)[0] = _mm_subs_epu8(((__m128i  *) Y)[0], m6);
                ((__m128i  *) Y)[1] = _mm_subs_epu8(((__m128i  *) Y)[1], m6);
                ((__m128i  *) Y)[2] = _mm_subs_epu8(((__m128i  *) Y)[2], m6);
                ((__m128i  *) Y)[3] = _mm_subs_epu8(((__m128i  *) Y)[3], m6);
            }
            unsigned char a188, a194;
            int a186, a205;
            short int s48, s49, s54, s55;
            const unsigned char  *a187, *a193, *b15;
            short int  *a204, *a206, *a207, *a223, *a224, *b16;
            __m128i  *a184, *a185, *a190, *a196, *a208, *a209, *a210
                    , *a211, *a212, *a215, *a225, *a226;
            __m128i a199, a200, a218, a219;
            __m128i a189, a191, a192, a195, a197, a198, a201
                    , a202, a203, a213, a214, a216, a217, a220, a221
                    , a222, d17, d18, d19, d20, m39, m40, m41
                    , m42, m43, m44, m45, m46, s46, s47, s50
                    , s51, s52, s53, s56, s57, t25, t26, t27
                    , t28, t29, t30;
            a184 = ((__m128i  *) Y);
            s46 = *(a184);
            a185 = (a184 + 2);
            s47 = *(a185);
            a186 = (4 * i9);
            b15 = (a186 + syms);
            a187 = (b15 + 2);
            a188 = *(a187);
            a189 = _mm_set1_epi8(a188);
            a190 = ((__m128i  *) Branchtab);
            a191 = *(a190);
            a192 = _mm_xor_si128(a189, a191);
            a193 = (b15 + 3);
            a194 = *(a193);
            a195 = _mm_set1_epi8(a194);
            a196 = (a190 + 2);
            a197 = *(a196);
            a198 = _mm_xor_si128(a195, a197);
            t25 = _mm_avg_epu8(a192,a198);
            a199 = ((__m128i ) t25);
            a200 = _mm_srli_epi16(a199, 2);
            a201 = ((__m128i ) a200);
            t26 = _mm_and_si128(a201, _mm_set_epi8(63, 63, 63, 63, 63, 63, 63
        , 63, 63, 63, 63, 63, 63, 63, 63
        , 63));
            t27 = _mm_subs_epu8(_mm_set_epi8(63, 63, 63, 63, 63, 63, 63
        , 63, 63, 63, 63, 63, 63, 63, 63
        , 63), t26);
            m39 = _mm_adds_epu8(s46, t26);
            m40 = _mm_adds_epu8(s47, t27);
            m41 = _mm_adds_epu8(s46, t27);
            m42 = _mm_adds_epu8(s47, t26);
            a202 = _mm_min_epu8(m40, m39);
            d17 = _mm_cmpeq_epi8(a202, m40);
            a203 = _mm_min_epu8(m42, m41);
            d18 = _mm_cmpeq_epi8(a203, m42);
            s48 = _mm_movemask_epi8(_mm_unpacklo_epi8(d17,d18));
            a204 = ((short int  *) dec);
            a205 = (8 * i9);
            b16 = (a204 + a205);
            a206 = (b16 + 4);
            *(a206) = s48;
            s49 = _mm_movemask_epi8(_mm_unpackhi_epi8(d17,d18));
            a207 = (b16 + 5);
            *(a207) = s49;
            s50 = _mm_unpacklo_epi8(a202, a203);
            s51 = _mm_unpackhi_epi8(a202, a203);
            a208 = ((__m128i  *) X);
            *(a208) = s50;
            a209 = (a208 + 1);
            *(a209) = s51;
            a210 = (a184 + 1);
            s52 = *(a210);
            a211 = (a184 + 3);
            s53 = *(a211);
            a212 = (a190 + 1);
            a213 = *(a212);
            a214 = _mm_xor_si128(a189, a213);
            a215 = (a190 + 3);
            a216 = *(a215);
            a217 = _mm_xor_si128(a195, a216);
            t28 = _mm_avg_epu8(a214,a217);
            a218 = ((__m128i ) t28);
            a219 = _mm_srli_epi16(a218, 2);
            a220 = ((__m128i ) a219);
            t29 = _mm_and_si128(a220, _mm_set_epi8(63, 63, 63, 63, 63, 63, 63
        , 63, 63, 63, 63, 63, 63, 63, 63
        , 63));
            t30 = _mm_subs_epu8(_mm_set_epi8(63, 63, 63, 63, 63, 63, 63
        , 63, 63, 63, 63, 63, 63, 63, 63
        , 63), t29);
            m43 = _mm_adds_epu8(s52, t29);
            m44 = _mm_adds_epu8(s53, t30);
            m45 = _mm_adds_epu8(s52, t30);
            m46 = _mm_adds_epu8(s53, t29);
            a221 = _mm_min_epu8(m44, m43);
            d19 = _mm_cmpeq_epi8(a221, m44);
            a222 = _mm_min_epu8(m46, m45);
            d20 = _mm_cmpeq_epi8(a222, m46);
            s54 = _mm_movemask_epi8(_mm_unpacklo_epi8(d19,d20));
            a223 = (b16 + 6);
            *(a223) = s54;
            s55 = _mm_movemask_epi8(_mm_unpackhi_epi8(d19,d20));
            a224 = (b16 + 7);
            *(a224) = s55;
            s56 = _mm_unpacklo_epi8(a221, a222);
            s57 = _mm_unpackhi_epi8(a221, a222);
            a225 = (a208 + 2);
            *(a225) = s56;
            a226 = (a208 + 3);
            *(a226) = s57;
            if ((((unsigned char  *) X)[0]>210)) {
                __m128i m12, m13;
                m12 = ((__m128i  *) X)[0];
                m12 = _mm_min_epu8(m12, ((__m128i  *) X)[1]);
                m12 = _mm_min_epu8(m12, ((__m128i  *) X)[2]);
                m12 = _mm_min_epu8(m12, ((__m128i  *) X)[3]);
                __m128i m14;
                m14 = _mm_min_epu8(_mm_srli_si128(m12, 8), m12);
                m14 = ((__m128i ) _mm_min_epu8(((__m128i ) _mm_srli_epi64(m14, 32)), ((__m128i ) m14)));
                m14 = ((__m128i ) _mm_min_epu8(((__m128i ) _mm_srli_epi64(m14, 16)), ((__m128i ) m14)));
                m14 = ((__m128i ) _mm_min_epu8(((__m128i ) _mm_srli_epi64(m14, 8)), ((__m128i ) m14)));
                m14 = _mm_unpacklo_epi8(m14, m14);
                m14 = _mm_shufflelo_epi16(m14, _MM_SHUFFLE(0, 0, 0, 0));
                m13 = _mm_unpacklo_epi64(m14, m14);
                ((__m128i  *) X)[0] = _mm_subs_epu8(((__m128i  *) X)[0], m13);
                ((__m128i  *) X)[1] = _mm_subs_epu8(((__m128i  *) X)[1], m13);
                ((__m128i  *) X)[2] = _mm_subs_epu8(((__m128i  *) X)[2], m13);
                ((__m128i  *) X)[3] = _mm_subs_epu8(((__m128i  *) X)[3], m13);
            }
        }
        /* skip */
    }

}

