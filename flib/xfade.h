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
        EQ_POWER, 
        ASYMMETRIC_MIX
    };

    void Init(float sr, float ramp_time_ms)
    {
        sr_ = sr;
        
        ramp_time_ms_ = ramp_time_ms;
        ramp_.Init(sr_);
        ramp_.Start(0.0f, 0.0f, ramp_time_ms_);

        SetCrossfadeType(TYPE::EQ_POWER); // default to power crossfade
    }

    float Process(const float sig_a, const float sig_b) {
        val_ = ramp_.Process(&ramp_finished_);

        if (type_ == TYPE::EQ_GAIN) {
            wa_ = 1 - val_;
            wb_ = val_;
        } else if (type_ == TYPE::EQ_POWER) {
            float theta = val_ * M_PI_2;
            wa_ = cosf(theta);
            wb_ = sinf(theta);
        } else if (type_ == TYPE::ASYMMETRIC_MIX) {
            if (val_ < 0.5f) {
                wa_ = 1.0f;
                wb_ = val_ * 2.f;
            } else {
                wa_ = (1 - ((val_-0.5f)*2.f));
                wb_ = 1.0f;
            }
        }

        return sig_a * wa_ + sig_b * wb_;
    }

    void SetCrossfadeType(TYPE type) { 
        type_ = type; 
    }
    
    void SetCrossfade(float x) { 
        if (x != val_) {
            ramp_.Start(val_, x, ramp_time_ms_ / 1000.0f); 
        }
    }

private:
    // config
    float sr_;
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
