#pragma once
#ifndef DSY_VIB_H
#define DSY_VIB_H

#ifdef __cplusplus


#include "daisysp.h"


namespace daisysp
{

/**  
    @brief Single Chorus engine. Used in Chorus.
    @author Ben Sergentanis
*/
class VibratoEngine
{
  public:
    VibratoEngine() {}
    ~VibratoEngine() {}

    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;
        del_.Init();
        feedback_ = .2f;
        SetDelay(.75);

        lfo_.Init(sample_rate);
        lfo_.SetWaveform(Oscillator::WAVE_SIN);
        lfo_.Reset();
        lfo_.SetFreq(0.5f); // 0.5 Hz
        lfo_.SetAmp(0.5f);  // 50% depth
    }

    float Process(float in)
    {
        fonepole(delay_, delay_target_, 0.00007f);

        fonepole(depth_, depth_target_, 0.00007f);
        
        lfo_.SetAmp(depth_);

        float lfo_sig = ProcessLfo() * delay_;
        // smooth delay time
        del_.SetDelay(lfo_sig + delay_);
        float out = del_.Read();
        del_.Write(in + out * feedback_);
        // return (in + out) * .5f; // equal mix
        return out;
    }

    void SetLfoDepth(float depth)
    {
        depth_target_ = depth;
    }

    void SetLfoFreq(float freq)
    {
        lfo_.SetFreq(freq);
    }

    void SetDelay(float delay)
    {
        delay = (.1f + delay * max_delay_ms_); // 0.1 to 8 ms
        SetDelayMs(delay);
    }

    void SetDelayMs(float ms)
    {
        ms = fmax(.1f, ms);
        // delay_ = ms * .001f * sample_rate_;
        delay_target_ = ms * 0.001f * sample_rate_;
    }

    // void SetMaxDelayMs(float ms)
    // {
    //     max_delay_ms_ = fmax(4.f, ms);
    //     max_delay_ms_ = fmin(kMaxDelayMs, max_delay_ms_);
    // }

    void SetFeedback(float feedback)
    {
        feedback_ = fclamp(feedback, 0.f, 1.f);
    }

  private:
    float sample_rate_;
    static constexpr int32_t kDelayLength = 2400; // 50ms @ 48kHz

    float feedback_ = 0.f;
    float delay_ = 0.f;
    float delay_target_ = 0.f; // target delay length (in samples)

    float depth_ = 0.f;         // current depth
    float depth_target_ = 0.f; // target depth

    float max_delay_ms_ = 40.f; // max delay time in ms

    Oscillator lfo_;

    DelayLine<float, kDelayLength> del_;

    float ProcessLfo()
    {
        return lfo_.Process();
    }
};

} // namespace daisysp

#endif // __cplusplus
#endif // DSY_VIB_H
