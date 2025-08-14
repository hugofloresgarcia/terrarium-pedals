#pragma once
#ifndef HUGO_LIB_LED_H
#define HUGO_LIB_LED_H

#ifdef __cplusplus

#include "daisy_petal.h"
#include "daisysp.h"

using namespace daisysp;

namespace daisy
{


class LedWrap {
public: 
    LedWrap() {}
    ~LedWrap() {}

    enum class LedState {
        OFF,
        ON,
        BLINKING,
        BLINK_SHORT,
    };

    void Init(Led led, float sample_rate) {
        led_ = led;
        blink_lfo_.Init(sample_rate);
        blink_lfo_.SetWaveform(Oscillator::WAVE_SQUARE); // square wave for blinking
        blink_lfo_.SetFreq(8.0f); // default blink rate of 8 Hz
    }

    void SetBlinkRate(float rate) {
        blink_lfo_.SetFreq(rate);
    }

    LedState GetState() const {
        return state_;
    }

    void SetState(LedState state, uint32_t blink_duration_ms = 0) {
        switch (state) {
            case LedState::OFF:
                led_.Set(0.0f);
                is_blinking_ = false;
                break;
            case LedState::ON:
                led_.Set(1.0f);
                is_blinking_ = false;
                break;
            case LedState::BLINKING:
                blink_lfo_.SetFreq(8.0f); // default blink frequency
                is_blinking_ = true;
                break;
            case LedState::BLINK_SHORT:
                is_blinking_ = true;
                blink_lfo_.SetFreq(8.0f); // faster blink for short blink effect
                if (state_ != LedState::BLINK_SHORT) {
                    blink_start_ms_ = System::GetNow();
                    blink_duration_ms_ = blink_duration_ms > 0 ? blink_duration_ms : 100; // default 100ms
                }
                break;
            default:
                is_blinking_ = false; // default to off if unknown state
                led_.Set(0.0f);
                break;
        }
        if (state_ != LedState::BLINK_SHORT) {
            prev_state_ = state_;
        }
        state_ = state;
    }
    
    void Process() {
        if (is_blinking_) {
            float blink_value = blink_lfo_.Process() < 0.0f ? 0.0f : 1.0f; // convert -1 to 0, 1 to 1
            led_.Set(blink_value);
            
            if (state_ == LedState::BLINK_SHORT) {
                uint32_t now = System::GetNow();
                if ((now - blink_start_ms_) >= blink_duration_ms_) {
                    SetState(prev_state_); // reset state after duration
                }
                // if (blink_value < 0.5f) {led_.Set(0.0f);} // keep LED dimmed during blink
            }
            led_.Update(); // update the LED state
        }
        else {
            led_.Update(); // update the LED state when not blinking
        }
    }

    void PrintDebugState(daisy::DaisyPetal &hw) {
        hw.seed.Print(" LED State: %-10s | Prev State: %-10s | Blinking: %d | Blink Freq: %.2f Hz", 
                          (state_ == LedState::OFF) ? "OFF" : 
                          (state_ == LedState::ON) ? "ON" : 
                          (state_ == LedState::BLINKING) ? "BLINKING" : 
                          (state_ == LedState::BLINK_SHORT) ? "BLINK_SHORT" : "UNKNOWN",
                          (prev_state_ == LedState::OFF) ? "OFF" : 
                          (prev_state_ == LedState::ON) ? "ON" : 
                          (prev_state_ == LedState::BLINKING) ? "BLINKING" : 
                          (prev_state_ == LedState::BLINK_SHORT) ? "BLINK_SHORT" : "UNKNOWN",
                          is_blinking_ ? 1 : 0,
                          0.0f);
    }

public: // FOR DEBUGGING ONLY
    Led led_;
    LedState state_ = LedState::OFF; // current state of the LED
    LedState prev_state_ = LedState::OFF; // previous state of the LED
    bool is_blinking_ = false; // Whether the LED is blinking
    long num_blinks_ = 0; // (Unused now but kept for debugging)

    Oscillator blink_lfo_; // LFO for blinking effect 

private:
    uint32_t blink_start_ms_ = 0;     // Time blink started, in milliseconds
    uint32_t blink_duration_ms_ = 0;  // Duration of short blink in milliseconds
};



} // namespace daisysp

#endif // __cplusplus
#endif // HUGO_LIB_LED_H
