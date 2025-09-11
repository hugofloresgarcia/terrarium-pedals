#pragma once
#ifndef HUGO_LIB_TAPTEMPO_H
#define HUGO_LIB_TAPTEMPO_H

#ifdef __cplusplus

#include "daisysp.h"

#include "stack.h"

using namespace daisy;

namespace daisysp
{


#define TAP_TEMPO_AVERAGES 5

// grabbed some ideas from https://github.com/schollz/taptempo/blob/main/main.cpp
class TapTempo
{
public:
    TapTempo() {}
    ~TapTempo() {}

    void Init(float sr) { 
        sr_ = sr; 
    }

    void Reset() {
        prev_periods_.clear();
    }

    void Tap() {
        uint32_t cur_tap_time = t;
        float interval_ms = ((cur_tap_time - last_tap_time_) / sr_) * 1000.0f;
        if (interval_ms > max_period_ms_ || interval_ms < min_period_ms_) {
            last_tap_time_ = cur_tap_time;
            // reset the tap tempo
            Reset();
            return;
        }
        last_tap_time_ = cur_tap_time;

        if (prev_periods_.full()) {
            float oldest;
            prev_periods_.pop(oldest);
        }
        prev_periods_.push(interval_ms);

        // weighted average of the previous intervals
        float weighted_avg_interval_ms = 0.0f;
        for (uint8_t i = 0; i < prev_periods_.size(); i++) {
            float weight = weights[i];
            float period;
            if (prev_periods_.peek(period, i)) {
                weighted_avg_interval_ms += period * weight;
            }
        }
        float denom = 0.0f;
        for (uint8_t i = 0; i < prev_periods_.size(); i++) {
            denom += weights[i];
        }
        weighted_avg_interval_ms /= denom;

        period_ms_ = weighted_avg_interval_ms;
        tempo_bpm_ = 60000.0f / period_ms_;
    }

    void Process() {
        t += 1;
    }

    float SetPeriodMs(float period) {
        period_ms_ = period;
        tempo_bpm_ = 60000.0f / period_ms_;
        return period_ms_;
    }

    float GetTempo() const { return tempo_bpm_; }
    float GetPeriodMs() const { return period_ms_; }

    void PrintDebugState(DaisyPetal& hw) {
        hw.seed.PrintLine("Tap Tempo BPM %f", tempo_bpm_);
        hw.seed.PrintLine("Tap Tempo ms %f", period_ms_);
        hw.seed.PrintLine("Last tap time %d", last_tap_time_);
        hw.seed.PrintLine("Current time %d", t);

    }

private:
    float sr_;

    uint32_t t = 0;
    uint32_t last_tap_time_ = 0;

    float tempo_bpm_ = 120.0f;
    float period_ms_ = 500.0f;

    static constexpr float min_period_ms_ = 100.0f;  // 100ms
    static constexpr float max_period_ms_ = 1000.0f; // 60bpm

    stack<float, TAP_TEMPO_AVERAGES> prev_periods_;
    const float weights[TAP_TEMPO_AVERAGES] = {1, 0.8, 0.6, 0.3, 0.1};
};

} // namespace daisysp

#endif // __cplusplus
#endif // HUGO_LIB_TAPTEMPO_H
