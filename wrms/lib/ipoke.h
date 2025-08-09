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

    void IPoke(float in, float index) 
    {

        // Region calculations
        float region_size  = end_pos_ - start_pos_;
        
        float val = in;
        float index1_ = index;
        float index0_next = index1_; // next idx0

        delay_line_.Write(val);

        // handle wrap around
        if (index1_ < index0_) {
            // index1_ += buf_size_; // E1?
            index1_ += region_size;
        }

        // get interpolation scalar
        float diff = fmax(index1_ - index0_, 1e-6f);
        float iscale = 1.0f / diff;

        // write samples
        size_t i_idx0 = (size_t)floor(index0_);
        size_t i_idx1 = (size_t)floor(index1_);

        // interpolated read from delay line
        for (size_t i = i_idx0; i < i_idx1; i++) {
            float a = (i - index0_)*iscale;
            float v = delay_line_.Read(1-a);
            
            size_t target_index = (
                start_pos_ + ((i - (size_t)start_pos_) % (size_t)region_size)
            );
            buf_[target_index] = v; // wrap around // E1?
        }

        // how many samples we recorded per input sample?
        // float samples_per_input = diff;
        index0_ = index0_next; // update index0 for next call
    }

    // read from the buffer with interpolation
    float Peek(float index){
        assert(buf_ != nullptr);
        
        size_t i_idx0 = (size_t)floor(index);
        float i_frac = index - (float)i_idx0;

        size_t region_start = (size_t)(start_pos_);
        size_t region_size  = GetRegionSize();

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
        return (size_t)((end_pos_ - start_pos_));
    }

    float GetStartPoint() const {
        return start_pos_;
    }

    float GetEndPoint() const {
        return end_pos_;
    }

    void Fill(float value)
    {
        assert(buf_ != nullptr);
        for (size_t i = 0; i < buf_size_; i++) {
            buf_[i] = value;
        }
    }

    // set the start and end points of the buffer region
    void SetRegion(float start_index, float end_index) {
        end_pos_ = fclamp(end_index, 1.f, buf_size_);
        start_pos_ = fclamp(start_index, 0.f, end_pos_-1.f);
    }

    void ResetIndex() {
        index0_ = start_pos_;
    }

private:
    float sr_;

    // buffer
    float* buf_ = nullptr;
    size_t buf_size_ = 0;

    // params
    float index0_ = 0.f; // 

    float start_pos_ = 0.f; 
    float end_pos_ = 1.f;

    DelayLine<float, 4> delay_line_; // delay line for interpolation
};

} // namespace daisysp

#endif // __cplusplus
#endif // FE_IPOKE_H
