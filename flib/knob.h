#pragma once
#ifndef HUGO_LIB_FKNOB_H
#define HUGO_LIB_FKNOB_H

#ifdef __cplusplus

#include "daisysp.h"
#include "daisy_petal.h"

namespace daisy
{

class FKnob
{
public:
    FKnob() {}
    ~FKnob() {};

    void Init(
        AnalogControl input, float min, float max, 
        Parameter::Curve curve, float sr
    ) {
        param_.Init(input, min, max, curve);
        sr_ = sr;
        change_window_samples_ = (size_t)(
            sr_ * change_window_ms_ / 1000.0f
        );
    }

    inline float Value() {
        return param_.Value();
    }

    void Process() {
        param_.Process();
        float val = input_.GetRawFloat();
    
        change_idx_++;
        if (change_idx_ >= change_window_samples_) {
            change_idx_ = 0;
            change_last_value_ = val;
        }

        if (fabs(val - change_last_value_) > change_threshold_) {
            moved_ = true;
        }
    }

    bool Moved() const {
        return moved_;
    }

    Parameter &p() {
        return param_;
    }

private:
    AnalogControl input_;
    Parameter param_;
    float sr_;

    bool moved_ = false;
    float change_threshold_ = 0.05f;

    float change_last_value_ = 0.0f;
    float change_window_ms_ = 50.0f;
    size_t change_window_samples_ = 0;
    size_t change_idx_ = 0;
};

} // namespace daisy

#endif // __cplusplus
#endif // HUGO_LIB_FKNOB_H
