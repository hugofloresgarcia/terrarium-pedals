#pragma once
#ifndef DSY_VIB_H
#define DSY_VIB_H

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
        SetDelayMs(40.f);

        lfo_.Init(sample_rate);
        lfo_.SetWaveform(Oscillator::WAVE_SIN);
        lfo_.Reset();
        lfo_.SetFreq(0.5f); // 0.5 Hz


    }

    float Process(float in)
    {
        fonepole(depth_, depth_target_, 0.00007f);
        // depth_ = depth_target_;
        this->SetDelayMs(max_delay_ms_ * depth_);
        
        fonepole(delay_, delay_target_, 0.00007f);

        float lfo_sig = linlin(lfo_.Process(), -1.f, 1.f, 0.f, depth_) * delay_;
        
        // smooth delay time
        del_.SetDelay(lfo_sig);
        float out = del_.Read();
        del_.Write(in + out * feedback_);

        fonepole(mix_, mix_target_, 0.00007f);
        mix_ = fclamp(mix_, 0.f, 1.f); // clamp mix to 0-1
        out = mix_ * out + (1.0f - mix_) * in; // mix input and output
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
        delay = (.05f + delay * max_delay_ms_); // 0.05 to 8 ms
        SetDelayMs(delay);
    }

    void SetDelayMs(float ms)
    {
        ms = fmax(.04f, ms);
        // delay_ = ms * .001f * sample_rate_;
        delay_target_ = ms * 0.001f * sample_rate_;
    }

    void SetFeedback(float feedback)
    {
        feedback_ = fclamp(feedback, 0.f, 1.f);
    }

    void SetMix(float mix)
    {
        mix_target_ = fclamp(mix, 0.f, 1.f);
    }

  private:
    float sample_rate_;
    static constexpr int32_t kDelayLength = 2400; // 50ms @ 48kHz

    float feedback_ = 0.f;
    float delay_ = 0.f;
    float delay_target_ = 0.f; // target delay length (in samples)

    float depth_ = 0.f;         // current depth
    float depth_target_ = 0.f; // target depth

    float max_delay_ms_ = 50.f; // max delay time in ms

    Oscillator lfo_;

    DelayLine<float, kDelayLength> del_;


    float mix_ = 1.0f; // mix level, 0.0 - 1.0
    float mix_target_ = 1.0f; // target mix level, 0.0 - 1.0
};

} // namespace daisysp

#endif // __cplusplus
#endif // DSY_VIB_H
