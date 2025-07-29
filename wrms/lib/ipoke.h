#pragma once
#ifndef FE_IPOKE_H
#define FE_IPOKE_H

#ifdef __cplusplus


#include "daisysp.h"



namespace daisysp
{

inline float linlin(float x, float a, float b, float c, float d) {
    if (x <= a)
        return c;
    if (x >= b)
        return d;
    return (x - a) / (b - a) * (d - c) + c;
}

class IpokeBuffer
{
public: 
    IpokeBuffer() {}
    ~IpokeBuffer() {}

    void Init(float sample_rate, 
        float* buffer, 
        size_t buffer_size
    ) {
        sr_ = sample_rate;
        buf_ = buffer;
        buf_size_ = buffer_size;

        delay_line_.Init();
        delay_line_.SetDelay((size_t)4); // 4 sample delay

        assert(buf_ != nullptr);
    }

    void IPoke(float in, float pos) 
    {
        // TODO: this function breaks when start_point_ and end_point_ are not 0 and 1
        // since the interpolation does not that that into account. 
        // see comments marked E1 as suspicious
        float pos_ = fclamp(pos, 0.f, 1.f);
        pos_ = linlin(pos_, 0.f, 1.f, start_point_, end_point_);

        // Region calculations
        float region_start = start_point_ * buf_size_;
        float region_end   = end_point_ * buf_size_;
        float region_size  = region_end - region_start;
        
        float val = in;
        float index1_ = pos_ * buf_size_;
        float index0_next = index1_; // next idx0

        delay_line_.Write(val);

        // handle wrap around
        if (index1_ < index0_) {
            // index1_ += buf_size_; // E1?
            index1_ += region_size;
        }

        // get interpolation scalar
        float diff = index1_ - index0_;
        if (diff < 0.000001f) {
            diff = 0.000001f; // avoid division by zero
        }
        float iscale = 1.0f / (diff);

        // write samples
        size_t i_idx0 = (size_t)floor(index0_);
        size_t i_idx1 = (size_t)floor(index1_);

        // interpolated read from delay line
        for (size_t i = i_idx0; i < i_idx1; i++) {
            float a = (i - index0_)*iscale;
            float v = delay_line_.Read(1-a);
            
            size_t target_index = (
                region_start + ((i - (size_t)region_start) % (size_t)region_size)
            );
            // buf_[i % buf_size_] = v; // wrap around // E1?
            buf_[target_index] = v; // wrap around // E1?
        }

        // how many samples we recorded per input sample?
        // float samples_per_input = diff;
        index0_ = index0_next; // update index0 for next call

    }

    // read from the buffer with interpolation
    float Peek(float pos){
        // adapt pos to start and end points
        pos = fclamp(pos, 0.f, 1.f);
        pos = linlin(pos, 0.f, 1.f, start_point_, end_point_);

        // // compute region start and size
        // size_t region_start = (size_t)(start_point_ * buf_size_);
        // size_t region_size  = (size_t)((end_point_ - start_point_) * buf_size_);

        // // compute index relative to region
        // float index = pos * buf_size_;
        // if(index < (float)region_start)
        //     index = (float)region_start;
        // if(index >= (float)(region_start + region_size))
        //     index = (float)(region_start + region_size - 1);

        // // Linear interpolation within region
        // size_t i_idx0 = (size_t)floor(index);
        // float i_frac  = index - (float)i_idx0;

        // const float a = buf_[(i_idx0) % buf_size_];
        // const float b = buf_[(i_idx0 + 1) % buf_size_];
        // return a + (b - a) * i_frac;

        float index = pos * buf_size_;
        assert(buf_ != nullptr);
        
        size_t i_idx0 = (size_t)floor(index);
        float i_frac = index - (float)i_idx0;

        // const float a = buf_[i_idx0 % buf_size_];
        // const float b = buf_[(i_idx0 + 1) % buf_size_];

        size_t region_start = (size_t)(start_point_ * buf_size_);
        size_t region_size  = (size_t)((end_point_ - start_point_) * buf_size_);

        size_t target_index_a = (
            region_start + ((i_idx0 - (size_t)region_start) % (size_t)region_size)
        );
        size_t target_index_b = (
            region_start + ((i_idx0 + 1 - (size_t)region_start) % (size_t)region_size)
        );

        float a = buf_[target_index_a]; 
        float b = buf_[target_index_b];

        return a + (b - a) * i_frac;
    } 

    

    size_t GetBufferSize() const {
        return buf_size_;
    }

    size_t GetRegionSize() const {
        return (size_t)((end_point_ - start_point_) * buf_size_);
    }

    void Fill(float value)
    {
        assert(buf_ != nullptr);
        for (size_t i = 0; i < buf_size_; i++) {
            buf_[i] = value;
        }
    }

    void SetStartPoint(float pos) {
        start_point_ = fclamp(pos, 0.f, 1.f);
    }

    void SetEndPoint(float pos) {
        end_point_ = fclamp(pos, 0.f, 1.f);
    }

private:
    float sr_;

    // buffer
    float* buf_ = nullptr;
    size_t buf_size_ = 0;

    // params
    float index0_ = 0.f; // 

    float start_point_ = 0.f; 
    float end_point_ = 1.f;

    DelayLine<float, 4> delay_line_; // delay line for interpolation
};

} // namespace daisysp

#endif // __cplusplus
#endif // FE_IPOKE_H
