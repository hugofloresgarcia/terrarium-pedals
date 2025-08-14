#pragma once
#ifndef WRMS_LIB_IPOKE_H
#define WRMS_LIB_IPOKE_H

#ifdef __cplusplus

#include "daisysp.h"

namespace daisysp
{

inline float zapgremlins(float x) {
    float absx = std::abs(x);
    // very small numbers fail the first test, eliminating denormalized numbers
    //    (zero also fails the first test, but that is OK since it returns zero.)
    // very large numbers fail the second test, eliminating infinities
    // Not-a-Numbers fail both tests and are eliminated.
    return (absx > 1e-15f && absx < 1e15f) ? x : 0.f;
}

class Ipoke 
{
public:
    Ipoke() {}
    ~Ipoke() {}

    void Init(float* buffer, 
              size_t buf_frames, 
              size_t buf_chans) {
        buf_ = buffer;
        frames_ = buf_frames;
        chans_ = buf_chans;

        values_.assign((size_t)chans_, 0.0f);
        coefficients_.assign((size_t)chans_, 0.0f);

        assert(buf_ != nullptr);
    }

    void ResetIndex() {
        last_index_ = -1;
        num_accumulated_ = 0;
    }


    void Poke(float index, const float* in) {
        float half_life = static_cast<long>(static_cast<float>(frames_) * 0.5f);

        if (index < 0.0) { // writing is stopped
            if (last_index_ >= 0) { // stop was just requested
                WriteAverageValue(last_index_);
                last_index_ = -1;
            }
        } else {
            // round to the next idx, make sure it's within bounds
            long indexl = (long)index;
            while (indexl >= (long)frames_) {
                indexl -= (long)frames_;
            }

            if (last_index_ < 0) { // first index we're writing! reset the avg and values
                last_index_ = indexl;
                num_accumulated_ = 0;
            }

            if (indexl == last_index_) { // index has not moved, accumulate
                                        // the value to average later.
                for (size_t chan = 0; chan < chans_; ++chan) {
                    values_[chan] += in[chan];
                }
                num_accumulated_ += 1;
            } else { // if it moves
                if (num_accumulated_ != 1) { // is there more than one value to avg
                    for (size_t chan = 0; chan < chans_; ++chan) {
                        values_[chan] /= num_accumulated_;
                    }
                    num_accumulated_ = 1; // reset to 1
                }

                for (size_t chan = 0; chan < chans_; ++chan) {
                    buf_[last_index_ * chans_ + chan] = zapgremlins(
                        (float)
                        (buf_[last_index_ * chans_ + chan] * overdub_ 
                            + values_[chan])
                    ); // write the avg value at the last index
                }

                long step = indexl - last_index_;

                if (step > 0) { // we are going up
                    if (step > half_life) {
                        step -= frames_; // wrap around
                        calculateCoefficients(step, in);
                        // fill the gap to zero
                        fillGap(last_index_ - 1, -1, -1);
                        // fill the gap from the top
                        fillGap(frames_ - 1, indexl, -1);
                    } else { // if not, just fill the gaps
                        calculateCoefficients(step, in);
                        fillGap(last_index_ + 1, indexl, 1);
                    }
                } else { // if we are going down
                    if (-step > half_life) { // is it faster to go the other way around?
                        step += frames_; // calculate the new number of steps
                        calculateCoefficients(step, in);
                        // fill the gap to the top
                        fillGap(last_index_ + 1, frames_, 1);
                        // fill the gap from zero
                        fillGap(0, indexl, 1);
                    } else { // if not, just fill the gaps
                        calculateCoefficients(step, in);
                        fillGap(last_index_ - 1, indexl, -1);
                    }
                }
                for (size_t chan = 0; chan < chans_; ++chan) {
                    values_[chan] = in[chan]; // transfer the new previous value
                }
            }
            last_index_ = indexl; // transfer the new previous address
        }
    }

    void SetOverdub(float overdub) {
        overdub_ = zapgremlins(overdub);
    }   

private:
    void WriteAverageValue(long index){
        for (size_t chan = 0; chan < chans_; ++chan) {
            buf_[index * chans_ + chan] =
                zapgremlins(static_cast<float>((buf_[index * chans_ + chan]
                    * overdub_) + (values_[chan] / num_accumulated_)));
            values_[chan] = 0.0f;
        }
    }

    void calculateCoefficients(long step, const float* in) {
        for (size_t chan = 0; chan < chans_; ++chan) {
            coefficients_[chan] = (in[chan] - values_[chan]) / step;
        }
    }

    void fillGap(long start, long end, long step) {
        d_start_ = start;
        d_end_   = end;
        d_step_  = step;
        long max_gaps_filled = 0;
        for (long i = start; i != end; i += step) {
            for (size_t chan = 0; chan < chans_; ++chan) {
                if (interpolate_) values_[chan] += coefficients_[chan];
                buf_[i * chans_ + chan] = zapgremlins(
                    (float)
                    (buf_[i * chans_ + chan] * overdub_ + values_[chan])
                );
                max_gaps_filled++;
            }
        }
        if (max_gaps_filled > d_max_gaps_filled_) {
            d_max_gaps_filled_ = max_gaps_filled;
        }
    }

    // debug variables
public:
    long d_start_;
    long d_end_;
    long d_step_;
    long d_max_gaps_filled_;

public:
    // buffer
    float* buf_ = nullptr;
    size_t frames_ = 0;
    size_t chans_ = 2;

    // params
    long last_index_ = -1; // last index written
    long num_accumulated_ = 0; // number of values accumulated for averaging

    // float *values_ = nullptr; // values to average
    // float *coefficients_ = nullptr; // coefficients for interpolation
    std::vector<float> values_; // vector version of values for easier management
    std::vector<float> coefficients_; // vector version of coefficients for easier management

    bool interpolate_ = true; // whether to interpolate or not
    float overdub_ = 0.0f; // feedback amount
};


class Ipeek
{
public:
    Ipeek() {}
    ~Ipeek() {}

    void Init(float* buffer, 
              size_t buf_frames, 
              size_t buf_chans) {
        buf_ = buffer;
        frames_ = buf_frames;
        chans_ = buf_chans;
        assert(buf_ != nullptr);
    }

    void Peek(float index, float* out) {
        for (size_t chan = 0; chan < chans_; ++chan) {
            float a, b, frac;
            size_t i_idx = (size_t)index;
            frac         = index - i_idx;
            a            = buf_[((i_idx    ) % frames_) * chans_ + chan];
            b            = buf_[((i_idx + 1) % frames_) * chans_ + chan];
            out[chan] = zapgremlins(a + (b - a) * frac);
        }
    }


private:
    // buffer
    float* buf_ = nullptr;
    size_t frames_;
    size_t chans_;
    
};

} // namespace daisysp

#endif // __cplusplus
#endif // WRMS_LIB_IPOKE_H