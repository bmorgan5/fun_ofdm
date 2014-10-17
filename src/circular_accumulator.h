/*! \file circular_accumulator.h
 *  \brief Template for a circular accumulator.
 *
 *  This class stores size samples of type T and
 *  keeps a running sum of the current samples.
 *  No indexing is needed, once the buffer is full
 *  each call to add() overwrites the oldest sample
 *  with the new one.
 */

#ifndef CIRCULAR_ACCUMULATOR_H
#define CIRCULAR_ACCUMULATOR_H

#include <vector>

namespace fun
{
    /*!
     * \brief The circular_accumulator template.
     *
     * This class is a template class for the circular accumulator
     * which is used to keep a running sum of the past #size samples
     */
    template<typename T>
    struct circular_accumulator
    {
        /*!
         * \brief The sum of the current samples in the #samples vector
         *
         * This is the key variable from this class that the user should
         * be interested in. In other words, it is essentially the only
         * public variable.
         */
        T sum;

        /*!
         * \brief The vector containing the past #size samples of type T
         *
         * This member is essentially private as the user should not interface
         * with it directly.
         */
        std::vector<T> samples;

        /*!
         * \brief The current index where the newest sample will be inserted
         * on the next call to #add().
         *
         * This variable is essentially private as the user should not interface
         * with it directly.
         */
        int index;

        /*!
         * \brief The max number of samples the accumulator holds at any give time.
         *
         * This variable is essentially private as the user should not interface
         * with it directly.
         */
        int size;

        /*!
         * \brief Constructor for circular_accumulator
         * \param _size The max number of samples that the accumulator can hold at
         * any given time.
         *
         * Initializes the #samples vector to contain #size elements and sets each
         * element to 0 along with the initial #sum.
         */
        circular_accumulator(int _size)
        {
            size = _size;
            samples.resize(size);
            index = 0;
            for(int x = 0; x < size; x++) samples[x] = T(0);
            sum = T(0);
        }

    //    circular_accumulator(){}

        /*!
         * \brief Adds a sample to the accumulator.
         * \param sample The sample to add to the accumulator
         *
         * If the accumulator is full the oldest sample in the
         * accumulator is overwritten by the new sample and the
         * current value of #sum is updated accordingly.
         */
        void add(T sample)
        {
            if(sample != sample) sample = 0;
            sum -= samples[index];
            sum += sample;
            samples[index++] = sample;
            if(index >= size) index = 0;
        }
    };

}

#endif // CIRCULAR_ACCUMULATOR_H
