#pragma once

#ifndef CENOTE_DELAY_H
#define CENOTE_DELAY_H

#include <cmath>
#include <cstdint>
#include "daisy_seed.h"
#include "daisysp.h"
#include "freqshift.h"

using namespace daisy;
using namespace daisysp;

namespace daisysp
{
class CenoteDelayEngine
{
  public:
    CenoteDelayEngine() {}
    ~CenoteDelayEngine() {}

    void Init(float sample_rate, float fade_time_ms = 20.0f)
    {
        sample_rate_ = sample_rate;

        del_.Init();
        freqshifter_.Init(sample_rate);

        feedback_ = 0.2f;
        SetDelayMs(1000.f);

        wet_        = 1.0f;
        wet_target_ = 1.0f;
        bypass_     = false;
        bypass_freqshift_ = false;

        const float fade_seconds = fmax(0.001f, fade_time_ms * 0.001f);
        wet_coeff_ = std::exp(-1.0f / (fade_seconds * sample_rate_));


        lopass_.Init(sample_rate_);
        lopass_.SetFreq(8000.0f);
        lopass_.SetRes(0.f);
        hipass_.Init(sample_rate_);
        hipass_.SetFreq(40.0f);
        hipass_.SetRes(0.f);

    }

    float Process(float in, bool clip = true, bool limit = false)
    {
        // smooth the wet sign
        fonepole(wet_, wet_target_, wet_coeff_);

        // smooth delay time
        fonepole(delay_, delay_target_, 0.00007f);
        del_.SetDelay(delay_);

        // read delayed sample
        float delayed = del_.Read();

        // write new sample to delay line
        float line_in = in + delayed * feedback_;

        // shift pitch
        if (bypass_freqshift_)
            freqshifter_.SetShift(0.0f);

        line_in = freqshifter_.Process(line_in);
            
        // filter edges
        lopass_.Process(line_in);
        line_in = lopass_.Low();

        hipass_.Process(line_in);
        line_in = hipass_.High();

        line_in = SoftClip(line_in);

        // apply limiter
        line_in = SoftLimit(line_in);

        del_.Write(line_in);

        // dry/wet
        const float out = delayed * wet_;

        return out;
    }

    void SetDelayMs(float ms)
    {
        ms = fmax(0.1f, ms);
        delay_target_ = ms * 0.001f * sample_rate_;
    }

    void SetFeedback(float feedback)
    {
        feedback_ = fclamp(feedback, 0.0f, 1.0f);
    }

    void SetBypass(bool should_bypass)
    {
        bypass_ = should_bypass;
        wet_target_ = bypass_ ? 0.0f : 1.0f;
    }

    void SetFadeTimeMs(float fade_time_ms)
    {
        const float fade_seconds = fmax(0.001f, fade_time_ms * 0.001f);
        wet_coeff_ = std::exp(-1.0f / (fade_seconds * sample_rate_));
    }

    float GetMaxDelayMs() const
    {
        return (kDelayLength / sample_rate_) * 1000.0f; // Convert samples to milliseconds
    }

    void SetBypassFrequencyShift(bool bypass)
    {
        bypass_freqshift_ = bypass;
    }

    void SetTransposition(float hz)
    {
        freqshifter_.SetShift(hz);
    }

  private:
    float sample_rate_;
    static constexpr int32_t kDelayLength = 2 * 48000; // enough for ~2 s @48kHz (adjust if needed)

    FrequencyShifter freqshifter_; // Pitch shifter engine, if needed
    bool bypass_freqshift_ = false; // Bypass frequency shifting

    Svf lopass_; // Low-pass filter for feedback smoothing
    Svf hipass_; // High-pass filter for feedback smoothing

    float feedback_;
    float delay_;         // smoothed current delay length (in samples)
    float delay_target_;  // target delay length (in samples)

    DelayLine<float, kDelayLength> del_;

    // bypass 
    bool  bypass_;
    float wet_;          
    float wet_target_;  
    float wet_coeff_;  

};
} // namespace daisysp



#endif // CENOTE_DELAY_H
