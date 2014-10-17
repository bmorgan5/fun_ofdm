/*! \file viterbi.h
 *  \brief Header file for the Viterbi class and the v struct.
 *
 *  The viterbi class contains methods for convolutionally encoding and decoding data
 *  using the viterbi algorithm.
 */

#ifndef VITERBI_H
#define VITERBI_H

#pragma once

#include <vector>
#include <stdlib.h>
#include <math.h>
#include <memory.h>

#include <pmmintrin.h>
#include <emmintrin.h>
#include <xmmintrin.h>
#include <mmintrin.h>

#define K 7
#define RATE 2
#define POLYS { 121, 91 }
#define NUMSTATES 64
#define DECISIONTYPE unsigned char
#define DECISIONTYPE_BITSIZE 8
#define COMPUTETYPE unsigned char

namespace fun
{
    //decision_t is a BIT vector

    /*! \brief decision_t is a BIT vector */
    typedef union {
        DECISIONTYPE   t[NUMSTATES/DECISIONTYPE_BITSIZE];
        unsigned int   w[NUMSTATES/32];
        unsigned short s[NUMSTATES/16];
        unsigned char  c[NUMSTATES/8];
    } decision_t __attribute__ ((aligned (16)));

    /*! \brief the metric_t attribute */
    typedef union {
        COMPUTETYPE t[NUMSTATES];
    } metric_t __attribute__ ((aligned (16)));

    /*!
     * \brief The v struct
     *
     *  This struct is used as a helper to keep track of parameters during decoding.
     */
    struct v {
        __attribute__ ((aligned (16))) metric_t metrics1; /* path metric buffer 1 */
        __attribute__ ((aligned (16))) metric_t metrics2; /* path metric buffer 2 */
        metric_t *old_metrics,*new_metrics; /* Pointers to path metrics, swapped on every bit */
        decision_t *decisions;   /* decisions */
        void (*update_blk)(struct v *vp, const COMPUTETYPE *syms, int nbits);
};

    /*!
     * \brief The viterbi class
     */
    class viterbi
    {
    private:

        COMPUTETYPE Branchtab[NUMSTATES/2*RATE] __attribute__ ((aligned (16)));

        void viterbi_chainback(struct v *vp,
              unsigned char *data, /* Decoded output data */
              unsigned int nbits, /* Number of data bits */
              unsigned int endstate) ;

        void FULL_SPIRAL(int nbits, unsigned char *Y, unsigned char *X, const unsigned char *syms, unsigned char *dec, unsigned char *Branchtab);

        /*!
         * \brief Create a new instance of a Viterbi decoder
         * \param len = FRAMEBITS (unpadded! data bits)
         */
        struct v *viterbi_alloc(int len);

        /*!
         * \brief Destroy instance of a Viterbi decoder
         * \param vp pointer to the v struct to free
         */
        void viterbi_free(struct v *vp);

        /*!
         * \brief Initialize decoder for start of new frame
         * \param vp pointer to v struct to initialize for decoding.
         * \param starting_state Initial state for viterbi decoder
         */
        void viterbi_init(struct v *vp, int starting_state);

        /*!
         * \brief Decode one frame worth of data.
         * \param vp Pointer to v struct used to store parameters and help with decoding
         * \param symbols Input symbol to be decoded
         * \param data Output data that has been decoded
         *
         * NOTE: nbits has to match what was passed to viterbi_alloc(...)
         * FIXME: store nbits in struct v?
         */
        void viterbi_decode(struct v *vp, const COMPUTETYPE *symbols, unsigned char *data, int nbits);

        /*! \brief set the viterbi decoder to use a specific implementation */
        void viterbi_update_blk_SPIRAL(struct v *vp, const COMPUTETYPE *syms, int nbits);
        //void viterbi_spiral(struct v *vp);

    public:

        /*!
         * \brief Decodes convolutionally encoded data using the viterbi algorithm.
         * \param symbols Coded symbols that need to be decoded.
         * \param data Output data that has been decoded.
         * \param data_bits Number of data bits that that should be left after decoding
         */
        void conv_decode(unsigned char * symbols, unsigned char * data, int data_bits);

        /*!
         * \brief Convolutionally encodeds data.
         * \param data The data to be coded.
         * \param symbols The coded output symbols.
         * \param data_bits The number of bits in the data input.
         */
        void conv_encode(unsigned char * data, unsigned char * symbols, int data_bits);
    };

}


#endif // VITERBI_H
