#pragma once
#ifndef HUGO_LIB_WINDOW_H
#define HUGO_LIB_WINDOW_H

#ifdef __cplusplus

#include "daisysp.h"
#include "ipoke.h"
#include <cmath>

namespace daisysp
{


class Window
{
  public:
    Window() {}

    void Init(float sample_rate)
    {
        sr_        = sample_rate;
        state_     = State::kOn;
        idx_       = 0;
        total_samps_ = 1;
        val_       = 1.0f;
    }

    void BeginFadeIn(float duration_ms)
    {
        total_samps_ = static_cast<size_t>((duration_ms * 0.001f) * sr_);
        if(total_samps_ < 1)
            total_samps_ = 1;
        state_ = State::kFadeIn;
        idx_   = 0;
    }

    void BeginFadeOut(float duration_ms)
    {
        total_samps_ = static_cast<size_t>((duration_ms * 0.001f) * sr_);
        if(total_samps_ < 1)
            total_samps_ = 1;
        state_ = State::kFadeOut;
        idx_   = 0;
    }

    float ProcessOneFrame()
    {
        switch(state_)
        {
            case State::kFadeIn:
            {
                float x  = M_PI * (static_cast<float>(idx_) / (float)total_samps_);
                val_     = HannRamp(x);
                idx_++;
                if(idx_ >= total_samps_)
                    state_ = State::kOn;
                break;
            }
            case State::kFadeOut:
            {
                float x  = M_PI * (static_cast<float>(idx_) / (float)total_samps_);
                val_     = 1.0f - HannRamp(x);
                idx_++;
                if(idx_ >= total_samps_)
                {
                    state_ = State::kOff;
                    val_   = 0.0f;
                }
                break;
            }
            case State::kOff: 
                val_ = 0.0f;
                break;
            case State::kOn: 
                val_ = 1.0f;
                break;
            default:
                break;
        }
        return val_;
    }

    bool IsOn() const { return state_ == State::kOn;  }
    bool IsOff() const { return state_ == State::kOff; }
    bool IsFadingIn() const { return state_ == State::kFadeIn; }
    bool IsFadingOut() const { return state_ == State::kFadeOut; }

    float Value() const { return val_; }

  private:
    enum class State
    {
        kOff,
        kFadeIn,
        kFadeOut,
        kOn
    };

    inline float HannRamp(float x) const
    {
        return 0.5f * (1.0f - cosf(x));
    }

    float   sr_          = 48000.0f;
    State   state_       = State::kOff;
    size_t  idx_         = 0;
    size_t  total_samps_ = 1;
    float   val_         = 1.0f;
};



} // namespace daisysp

#endif // __cplusplus
#endif // HUGO_LIB_WINDOW_H
