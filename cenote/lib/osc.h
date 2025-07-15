
#pragma once

#ifndef DSY_HUGOOSC_H
#define DSY_HUGOOSC_H

#include <cmath>
#include <cstdint>
#include "daisy_seed.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

namespace daisysp
{


class OscillatorBase {
public:
    virtual ~OscillatorBase() {}

    virtual void SetFreq(float freq) = 0;
    virtual void SetAmp(float amp) = 0;
    virtual float Process() = 0;
};

class LFO : public OscillatorBase {
public:
    LFO() : osc(), last_value(0.0f) {}

    void Init(float sample_rate) {
        osc.Init(sample_rate);
        osc.SetWaveform(osc.WAVE_SIN);
    }

    void SetFreq(float freq) override {
        osc.SetFreq(freq);
    }

    void SetAmp(float amp) override {
        osc.SetAmp(amp);
    }
    
    void SetWaveform(uint8_t waveform) {
        osc.SetWaveform(waveform);
    }

    void Reset(float phase = 0.0f) {
        osc.Reset(phase);
    }

    float Process() override {
        last_value = osc.Process();
        return last_value;
    }

private:
    Oscillator osc;
    float last_value;
};

class Noise : public OscillatorBase {
public:
    Noise() : last_value(0.0f), amp(1.0f), hold_rate(1.0f), last_sample_time(0.0f) {}

    void Init(float sample_rate) {
        // Set the sample rate and initial parameters for hold functionality
        sample_rate_ = sample_rate;
    }

    void SetFreq(float freq) override {
        // Set frequency to control the sample rate of the sample and hold
        hold_rate = freq;
    }

    void SetAmp(float amp) override {
        this->amp = amp;
    }

    float Process() override {
        // Generate white noise
        float noise_value = GenerateNoise();

        // Sample-and-hold logic (simple time-based thresholding)
        if (System::GetNow() - last_sample_time >= (1.0f / hold_rate)) {
            last_value = noise_value;
            last_sample_time = System::GetNow();
        }

        // Return the processed value multiplied by the amplitude
        return last_value * amp;
    }

    void Reset(float phase = 0.0f) {
        // Reset the last value and sample time
        last_value = phase;
        last_sample_time = System::GetNow();
    }

private:
    // Simple noise generator using random values (you could use a more sophisticated generator here)
    float GenerateNoise() {
        // This is a simple noise function, using the system's time for randomness
        // You could replace this with a better noise function (e.g., Perlin noise, white noise, etc.)
        return static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.0f - 1.0f; // Generates a value between -1 and 1
    }

    float last_value;
    float amp;
    float hold_rate;
    float last_sample_time;
    float sample_rate_;  // Used to track sample rate, could be useful for interpolation or smoothing
};


class WaveGenerator {
public:
    WaveGenerator() {}

    enum
    {
        WAVE_SIN,
        WAVE_TRI,
        WAVE_SAW,
        WAVE_RAMP,
        WAVE_SQUARE,
        WAVE_POLYBLEP_TRI,
        WAVE_NOISE,
        WAVE_LAST,
    };


    void Init(float sr) {
        lfo_.Init(sr);
        noise_.Init(sr);
    }

    void SetFreq(float freq) {
        lfo_.SetFreq(freq);
        noise_.SetFreq(freq/30000.0f);
    }

    void SetAmp(float amp) {
        lfo_.SetAmp(amp);
        noise_.SetAmp(amp);
    }

    void SetWaveform(uint8_t waveform) {
        if (waveform > 6) {
            // we're using noise here.
            is_noise_ = true;
        }
        else {
            lfo_.SetWaveform(waveform);
            is_noise_ = false;
        }
    }

    void Reset(float phase = 0.0f) {
        lfo_.Reset(phase);
        noise_.Reset();
    }

    float Process() {
        if (is_noise_) {
            return noise_.Process();
        } else {
            return lfo_.Process();
        }
    }

private:
    LFO lfo_;      // Pointer to the LFO oscillator
    Noise noise_;  // Pointer to the Noise oscillator
    
    uint8_t waveform_;  // Current waveform type
    bool is_noise_ = false;  // Flag to indicate if noise is being used
};

} // namespace daisysp
