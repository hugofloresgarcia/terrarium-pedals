#pragma once
#ifndef HUGO_LIB_LED_H
#define HUGO_LIB_LED_H

#ifdef __cplusplus

#include "daisy_petal.h"
#include "daisysp.h"
#include <cmath>

namespace daisysp
{

class LedWrap {
public:
    enum class LedState { OFF, ON, BLINKING, BLINK_SHORT };

    LedWrap() = default;
    ~LedWrap() = default;

    // controlRateHz = how often Process() will be called (your "control block rate")
    // e.g. on pedal: sr / audioBlockSize; in JUCE: your timer rate (e.g. 60 or 200 Hz)
    void Init(daisy::Led& led, float controlRateHz)
    {
        led_ = &led;
        ctrl_rate_hz_ = (controlRateHz > 0.f ? controlRateHz : 60.f);
        tick_ms_      = 1000.0f / ctrl_rate_hz_;
        elapsed_ms_   = 0.0f;

        blink_lfo_.Init(ctrl_rate_hz_);                // DaisySP osc works fine at control rate
        blink_lfo_.SetWaveform(Oscillator::WAVE_SQUARE);
        SetBlinkRate(8.0f);                            // default 8 Hz blink
        SetState(LedState::OFF);
    }

    void SetBlinkRate(float rateHz)
    {
        blink_freq_hz_ = (rateHz > 0.f ? rateHz : 1.0f);
        blink_lfo_.SetFreq(blink_freq_hz_);
    }

    LedState GetState() const { return state_; }

    // blinkDurationMs only used for BLINK_SHORT (defaults to 100ms)
    void SetState(LedState state, uint32_t blinkDurationMs = 100)
    {
        if (!led_) return;

        switch (state)
        {
            case LedState::OFF:
                led_->Set(0.0f);
                is_blinking_ = false;
                break;

            case LedState::ON:
                led_->Set(1.0f);
                is_blinking_ = false;
                break;

            case LedState::BLINKING:
                is_blinking_ = true;
                blink_lfo_.SetFreq(blink_freq_hz_); // keep current blink rate
                break;

            case LedState::BLINK_SHORT:
                is_blinking_      = true;
                blink_lfo_.SetFreq(std::max(8.0f, blink_freq_hz_)); // fastish by default
                // start short-blink window from "now"
                blink_start_ms_   = elapsed_ms_;
                blink_duration_ms_ = (blinkDurationMs > 0 ? blinkDurationMs : 100);
                break;
        }

        if (state_ != LedState::BLINK_SHORT)
            prev_state_ = state_;
        state_ = state;
    }

    // Call this once per control tick (pedal control loop or JUCE Timer)
    void Process()
    {
        if (!led_) return;

        // advance our internal time
        elapsed_ms_ += tick_ms_;

        if (is_blinking_)
        {
            // Oscillator::Process() returns -1..+1; map to 0/1
            const float s = blink_lfo_.Process();
            led_->Set(s >= 0.0f ? 1.0f : 0.0f);

            if (state_ == LedState::BLINK_SHORT)
            {
                if ((elapsed_ms_ - blink_start_ms_) >= blink_duration_ms_)
                {
                    SetState(prev_state_); // restore after short blink
                }
            }
            led_->Update();
        }
        else
        {
            // keep hardware LED in sync even when static
            led_->Update();
        }
    }

    // Optional: call this from your pedal’s debug print loop
    template <typename Printer>
    void PrintDebugState(Printer&& printFn) const
    {
        const char* s  = (state_ == LedState::OFF)        ? "OFF" :
                         (state_ == LedState::ON)         ? "ON" :
                         (state_ == LedState::BLINKING)   ? "BLINKING" : "BLINK_SHORT";
        const char* ps = (prev_state_ == LedState::OFF)        ? "OFF" :
                         (prev_state_ == LedState::ON)         ? "ON" :
                         (prev_state_ == LedState::BLINKING)   ? "BLINKING" : "BLINK_SHORT";
        printFn(" LED State: %-10s | Prev: %-10s | Blink: %d | Rate: %.2f Hz\n",
                s, ps, is_blinking_ ? 1 : 0, blink_freq_hz_);
    }

private:
    daisy::Led* led_ = nullptr;

    LedState state_      = LedState::OFF;
    LedState prev_state_ = LedState::OFF;
    bool     is_blinking_ = false;

    Oscillator blink_lfo_;
    float      blink_freq_hz_ = 8.0f;

    // emulator-friendly timebase
    float ctrl_rate_hz_  = 60.0f;
    float tick_ms_       = 1000.0f / 60.0f;
    float elapsed_ms_    = 0.0f;
    float blink_start_ms_ = 0.0f;
    uint32_t blink_duration_ms_ = 100;
};

} // namespace daisysp

#endif // __cplusplus
#endif // HUGO_LIB_LED_H
