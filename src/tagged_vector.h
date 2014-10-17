/*! \file tagged_vector.h
 *  \brief Header file for the tagged_vector template.
 *
 * This file contains the template classes for tagged vectors
 * and tagged samples that are used in the receiver chain's
 * input and output buffers.
 *
 */

#ifndef TAGGED_VECTOR_H
#define TAGGED_VECTOR_H

#include <vector>
#include <complex>
#include <assert.h>

namespace fun
{
    /*!
     * \brief The vector_tag enum
     *
     * These tags are used to mark specific locations in the received packet
     * such as the start of the short & long training sequences
     */
    enum vector_tag
    {
        NONE,           //!< No tag
        STS_START,      //!< Approximate start of STS
        STS_END,        //!< Approximate end of STS
        LTS_START,      //!< Estimated beginning of LTS (i.e. first sample of the LTS Cyclic prefix
        LTS1,           //!< Estimated beginning of first LTS symbol (32 samples after LTS_START
        LTS2,           //!< Estimated beginning of second LTS symbol (64 samples after LTS1
        START_OF_FRAME, //!< Estimated beginning of frame i.e. Signal symbol (64 samples after LTS2)
    };

    /*! \brief tagged_vector struct
     *
     * An array of N complex doubles with a meta-data tag
     * Note: tagged_vector's are not meant to be resized
     *
     */
    template<int N>
    struct tagged_vector
    {

        std::complex<double> samples[N]; //!< The array of N complex doubles
        vector_tag tag;                  //!< The array's tag

        /*!
         * \brief Non-initializing constructor for tagged_vector.
         *
         * Does not initialize the elements of #samples to anything.
         * Initializes #tag to _tag defaulting to #NONE if left out.
         *
         * \param _tag optional initial #tag value. Default is #NONE if left out.
         * Default is NONE if left out
         */
        tagged_vector(vector_tag _tag = NONE) { tag = _tag; }

        /*!
         * \brief Initializing constructor for tagged_vector
         *
         * Initializes the elements of #samples to _samples.
         * Initializes #tag to _tag defaulting to #NONE if left out.
         *
         * \param _samples initial samples to populate the elements of #samples with
         * \param _tag optional initial #tag value. Default is #NONE if left out.
         */
        tagged_vector(std::vector<std::complex<double> > _samples, vector_tag _tag = NONE)
        {
            assert(_samples.size() == N);
            memcpy(&samples[0], &_samples[0], _samples.size() * sizeof(std::complex<double>));
            tag = _tag;
        }
    };

    /*!
     * \brief The tagged_sample struct
     *
     * A single complex double with a meta-data tag
     */
    struct tagged_sample
    {
        std::complex<double> sample; //!< The complex sample
        vector_tag tag;              //!< The sample's tag

        /*!
         * \brief Constructor for tagged_sample
         *
         * Does not initialize the sample to anything
         * Initializes the tag to #NONE
         */
        tagged_sample() { tag = NONE; }
    };
}

#endif // TAGGED_VECTOR_H
