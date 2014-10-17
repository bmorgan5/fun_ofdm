/*! \file block.h
 *  \brief Base class for receiver chain blocks.
 *
 *  This class initializes the input & output buffers
 *  and contains the purely virtual work function.
 *
 */

#ifndef BLOCK_H
#define BLOCK_H

/*! \def BUFFER_MAX
 *  \brief Maximum buffer size of for the input & output buffers
 *
 *  Techinically each block uses the
 * ~~~{.cpp}
 * std::vector::reserve(size_type n)
 * ~~~
 * function to reserve BUFFER_MAX * sizeof(size_type) bytes
 */

#define BUFFER_MAX 65536

#include <vector>
#include <string>

namespace fun
{
    /*!
     * \brief The block_base class.
     *
     * This class is to allow the receiver chain to use generic pointers
     * to refer to each block in the receive chain even if they are
     * different templates.
     */
    class block_base
    {
    public:

        /*!
         * \brief block_base constructor
         * \param block_name the name of the block as a std::string
         */
        block_base(std::string block_name) :
            name(block_name)
        {
        }

        /*!
         * \brief The main work function.
         *
         * The work function is a pure virtual function.
         */
        virtual void work() = 0;

        /*!
         * \brief the public name of the block
         */
        std::string name;
    };

    /*!
     * \brief The block class template.
     *
     * This class is a template class for the blocks in the receiver chain.
     * This class contains the input and output buffers
     */
    template<typename I, typename O>
    class block : public block_base
    {
    public:

        /*!
         * \brief constructor
         *
         * Reserves BUFFER_MAX * sizeof(size_type) for the input and output buffers.
         * \param block_name the name of the block as a std::string
         */
        block(std::string block_name) :
            block_base(block_name)
        {
            input_buffer.reserve(BUFFER_MAX);
            output_buffer.reserve(BUFFER_MAX);
        }

        /*!
         * \brief The main work function.
         *
         * This function is purely virtual.
         * This function must consume input_buffer and fill output_buffer.
         * In doing so it should be sure to resize the output_buffer accordingly and
         * carryover any items from the input_buffer that it might need on its next call.
         */
        virtual void work() = 0;

        /*!
         * \brief input_buffer contains new input items to be consumed
         *
         * Contains new input items of type I. There is no guarantee on the number of items
         * passed to the input_buffer for each call to work except that it must be less than
         * #BUFFER_MAX.
         */
        std::vector<I> input_buffer;

        /*!
         * \brief output_buffer is where the output items of the block should be placed
         *
         * There is no restriction on the number of output items a block must produce on each call
         * except that it must be less than #BUFFER_MAX.
         */
        std::vector<O> output_buffer;
    };

}

#endif // BLOCK_H
