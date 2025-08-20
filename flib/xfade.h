#pragma once
#ifndef HUGO_LIB_XFade_H
#define HUGO_LIB_XFade_H

#ifdef __cplusplus

#include "daisysp.h"

namespace daisysp
{

// crossfade ugen.
class Xfade
{

public:
    Xfade() {}
    ~Xfade() {}

    enum class TYPE {
        EQ_GAIN, 
        EQ_POWER
    };

    void Init(float sr, size_t num_channels, float ramp_time_ms)
    {
        sr_ = sr;
        chans_ = num_channels;
        
        ramp_time_ms_ = ramp_time_ms;
        ramp_.Init(sr_);
        ramp_.Start(0.0f, 0.0f, ramp_time_ms_);

    }

    void ProcessFrame(const float* sig_a,const float* sig_b, float* out) {
        float x = ramp_.Process(&ramp_finished_);

        if (type_ == TYPE::EQ_GAIN) {
            wa_ = 1 - x;
            wb_ = x;
        } else if (type_ == TYPE::EQ_POWER) {
            float theta = x * M_PI_2;
            wa_ = cosf(theta);
            wb_ = sinf(theta);
        }

        for (size_t c = 0; c < chans_; c++) {
            out[c] = sig_a[c] * wa_ + sig_b[c] * wb_;
        }
    }

    void SetCrossfadeType(TYPE type) { type_ = type; }

    void SetCrossfade(float x) { ramp_.Start(val_, x, ramp_time_ms_); }

private:
    // config
    float sr_;
    size_t chans_;
    TYPE type_;

    // math
    float val_;
    float wa_;
    float wb_;

    // ramp (to avoid clicks)
    Line ramp_;
    uint8_t ramp_finished_;
    float ramp_time_ms_;
};

} // namespace daisysp

#endif // __cplusplus
#endif // HUGO_LIB_XFade_H
