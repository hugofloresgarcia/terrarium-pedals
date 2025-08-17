#pragma once
#ifndef HUGO_LIB_GRAINS_H
#define HUGO_LIB_GRAINS_H

#ifdef __cplusplus

#include "daisysp.h"
#include "ipoke.h"
#include "daisy_petal.h"
#include <array>

using namespace daisy;

namespace daisysp
{

class Grain 
{
public:
    Grain() {}
    ~Grain() {}

    enum class State
    {
        IDLE, 
        PLAYING, 
    };

    void Init(float sample_rate, 
         float* buffer, 
         size_t buf_frames, 
         size_t buf_chans) {
        sr_ = sample_rate;

        assert(buffer != nullptr); // make sure the buffer is not null
        buf_ = buffer; // pointer to the buffer
        frames_ = buf_frames;
        chans_ = buf_chans;

        peeker_.Init(buffer, buf_frames, buf_chans);
        env_.Init(sr_);

        pos_ = 0.f;
        rate_st_ = 0.f;
        dur_ms_ = 80.f;

        state_ = State::IDLE;
    }

    State state() const {
        return state_;
    }



    // trigger the grain. 
    // pos_samples is the position in samples to start the grain from
    // rate_st is the pitch shift in semitones
    // dur_ms is the duration of the grain in milliseconds
    // env_atk is the attack time in seconds (default 0.01s)
    void Trigger(float pos_samples, float rate_st, float dur_ms, float env_atk = 0.01f) {
        pos_ = pos_samples;
        rate_st_ = rate_st;
        dur_ms_ = dur_ms;
        env_atk_ = env_atk;
        start_pos_ = pos_samples;
        end_pos_virtual_ = pos_samples + (dur_ms * sr_ * 0.001f); // end position in samples
        end_pos_wrapped_ = WrapPos(end_pos_virtual_); // end position wrapped around the buffer

        float atk_time = fclamp(env_atk_ * dur_ms_, 2.f, dur_ms_-2.f); // min 10ms atk
        float decay_time = fclamp(dur_ms_ - atk_time, 2.f, dur_ms_); // min 5ms decay

        env_.SetTime(ADENV_SEG_ATTACK, atk_time * 0.001f);
        env_.SetTime(ADENV_SEG_DECAY, decay_time * 0.001f);
        env_.SetMin(0.f);
        env_.SetMax(1.f);
        env_.Trigger();

        state_ = State::PLAYING;
    }

    float WrapPos(float pos) {
        while (pos < 0.f) pos += frames_;
        while (pos >= frames_) pos -= frames_;
        return pos;
    }

    void ProcessOneFrame(float *out) {
        // Process the input buffer and produce output
        if (state_ == State::IDLE) {
            // do nothing
            for (size_t chan = 0; chan < chans_; ++chan) {
                out[chan] = 0.f;
            }
        } else if (state_ == State::PLAYING) {
            // get envelope value
            float env_val = env_.Process();
            
            // read from the buf
            peeker_.Peek(pos_, out);

            // apply the envelope to the output
            for (size_t chan = 0; chan < chans_; ++chan) {
                out[chan] *= env_val; // apply envelope
            }
        
            // calculate the increment based on the rate
            float inc = powf(2.f, rate_st_ / 12.0f);
            pos_ += inc;
            pos_ = WrapPos(pos_); // wrap around if needed
            if (pos_ >= end_pos_wrapped_) {
                pos_ = start_pos_; // reset to start position if we reach the end
                // TODO: the above maybe should be a flag since it leads to musically different effects. 
            }
                        
            // if the env is done, stop the grain
            if (!env_.IsRunning()) {
                state_ = State::IDLE;
            }

        }
    }

    void PrintDebugState(daisy::DaisyPetal &hw) {
        hw.seed.Print(" %-5s | %10.2f | %8.2f | %9.2f | %13.2f | %     .2f", 
                          (state_ == State::IDLE) ? "IDLE" : "PLAYING",
                          start_pos_,
                          end_pos_virtual_,
                          rate_st_,
                          dur_ms_,
                          env_atk_);
    }

public: 
    State state_;
    AdEnv env_;

    float sr_;
    float *buf_ = nullptr; // pointer to the buffer
    size_t frames_ = 0; // number of frames in the buffer
    size_t chans_ = 0; // number of channels in the buffer

    Ipeek peeker_;
    
    // playhead
    float pos_;
    
    // direction
    float direction_ = 1.f; // 1 for forward, -1 for backward

    float start_pos_ = 0.f; // start position in samples
    float end_pos_virtual_ = 0.f; // end position in samples (not wrapped)
    float end_pos_wrapped_ = 0.f; // end position wrapped around the buffer

    float rate_st_;
    float dur_ms_;
    float env_atk_ = 0.01f; // fraction of the duration for attack time (smaller values = faster attack)
};

class Grains
{
public:
    Grains() {}
    ~Grains() {}

    void Init(float sample_rate, 
              float* buffer, 
              size_t buf_frames, 
              size_t buf_chans) {
        sr_ = sample_rate;
        assert(buffer != nullptr); // make sure the buffer is not null
        buf_ = buffer;
        frames_ = buf_frames;
        chans_ = buf_chans;

        for (auto &g : grains_) {
            g.Init(sr_, buf_, frames_, chans_);
        }

        sig_data.assign(1 * chans_, 0.f); // a single frame buffer.
        sig_ = sig_data.data(); // pointer to the single frame buffer
    }

    void TriggerGrain(float pos_samples, 
                      float rate_st, 
                      float dur_ms, 
                      float env_atk = 0.01f, 
                      bool steal = true) {
        // clear any grains that are no longer busy from the busy list
        const auto notBusyAnymore = [this](size_t idx) {
            return grains_[idx].state() != Grain::State::PLAYING;
        };
        busy_grain_idxs.erase(
            std::remove_if(
                busy_grain_idxs.begin(), busy_grain_idxs.end(), 
                notBusyAnymore
            ), 
            busy_grain_idxs.end()
        );

        // trigger the first available grain
        for (size_t i = 0; i < grains_.size(); ++i)
        {
            if (grains_[i].state() == Grain::State::IDLE) {
                grains_[i].Trigger(pos_samples, rate_st, dur_ms, env_atk);
                busy_grain_idxs.insert(busy_grain_idxs.begin(), i); // add it to the busy list
                return; // only trigger one grain at a time
            }
        }

        // if no idle grain found, steal the last triggered grain
        if (steal && !busy_grain_idxs.empty()) {
            size_t idx = busy_grain_idxs.back(); // get the last busy grain
            grains_[idx].Trigger(pos_samples, rate_st, dur_ms, env_atk);
            busy_grain_idxs.pop_back(); // remove it from the busy list
            busy_grain_idxs.insert(busy_grain_idxs.begin(), idx);
            return;
        }
    }

    void ProcessOneFrame(float *out) {
        // Process all grains and produce output
        // zero the output buffer
        for (size_t chan = 0; chan < chans_; ++chan) {
            out[chan] = 0.f;
            sig_[chan] = 0.f;
        }

        for (auto &g : grains_) {
            for (size_t chan = 0; chan < chans_; ++chan) {
                sig_[chan] = 0.f; // reset the single frame buffer
            }
            g.ProcessOneFrame(sig_);
            // add the sig to the output
            for (size_t chan = 0; chan < chans_; ++chan) {
                out[chan] += sig_[chan]; // accumulate the output
            }
        }
    }

public:
    float sr_;
    float *buf_;
    size_t frames_;
    size_t chans_;

    std::array<Grain, 4> grains_; // array of grains, can be adjusted
    std::vector<size_t> busy_grain_idxs; // indices of busy grains

    std::vector<float> sig_data; // signal buffer for processing
    float *sig_; // a single frame buffer for output
    
};

} // namespace daisysp

#endif // __cplusplus
#endif // HUGO_LIB_GRAINS_H
