/*! \file frame_decoder.h
 *  \brief Header file for the Frame Decoder block and the FrameData struct.
 *
 * The Frame Decoder block is in charge of decoding the frame header and then the frame body.
 * This includes demodulating, deinterleaving, de-convolutional-coding, and descrambling.
 * First these must all be done to the header so as to get the correct rate and length of
 * the payload, then the payload must be decoded as well. If the block is succesful in
 * decoding the frame as determined by an IEEE CRC-32 check the payload is passed into
 * the output_buffer as unsigned char's or bytes.
 */

#ifndef FRAME_DECODER_H
#define FRAME_DECODER_H

#include <complex>
#include <deque>

#include "tagged_vector.h"
#include "rates.h"
#include "block.h"

namespace fun
{
    /*!
     * \brief The FrameData struct
     *
     * This struct keeps track of frame parameters such as length etc.
     */
    struct FrameData
    {
      int sample_count;                          //!< Number of samples in this frame
      int samples_copied;                        //!< Number of samples already copied
      RateParams rate_params;                    //!< Rate parameters for this frame
      std::vector<std::complex<double> > samples; //!< Decoded Samples
      int length;                                //!< Data length
      int required_samples;                      //!< Number of samples required to decode frame

      /*!
       * \brief Constructor for FrameData
       * \param _rate_params the rate parameters for this frame
       */
      FrameData(RateParams _rate_params) :
        rate_params(_rate_params)
      {
        samples.reserve(100000);
      }

      /*!
       * \brief Resets the parameters of the object to the new parameters provided
       * \param _rate_params new rate parameters for this frame
       * \param _sample_count new sample count for this frame
       * \param _length new length for this frame
       *
       * Also calculates #required_samples from the above parameters and resets
       * #samples_copied to 0.
       */
      void Reset(RateParams _rate_params, int _sample_count, int _length)
      {
          rate_params = _rate_params;
          length = _length;
          sample_count = _sample_count;
          required_samples = _sample_count / _rate_params.bpsc;
          samples_copied = 0;
      }
    };

    /*!
     * \brief The frame_decoder block.
     *
     * Inputs tagged_vector<48> from phase_tracker block.
     * Outputs std::vector<unsigned char> back to the receiver chain
     *
     * The Frame Decoder block is in charge of decoding the frame header and then the frame body.
     * This includes demodulating, deinterleaving, de-convolutional-coding, and descrambling.
     * First these must all be done to the header so as to get the correct rate and length of
     * the payload, then the payload must be decoded as well. If the block is succesful in
     * decoding the frame as determined by an IEEE CRC-32 check the payload is passed into
     * the output_buffer as unsigned char's or bytes.
     */
    class frame_decoder : public fun::block<tagged_vector<48>, std::vector<unsigned char> >
    {
    public:

        frame_decoder(); //!< Constructor for frame_decoder block.

        virtual void work(); //!< Signal processing happens here.

    private:

        FrameData m_current_frame; //!< Current frame that is being decoded.

    };

}
#endif // FRAME_DECODER_H

