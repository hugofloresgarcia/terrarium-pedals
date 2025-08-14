#pragma once
#ifndef HUGO_LIB_GRAINS_H
#define HUGO_LIB_GRAINS_H

#ifdef __cplusplus

#include "daisysp.h"
#include "ipoke.h"
#include "stack.h"

// pedal UI template
// --------------------------
// *(?)* | *(?)* | *(?)* 
// *(?)* | *(?)* | *(?)*
//
// (?) | (?) | (?) | (?)
//
// sw(?) | sw(?) 
// --------------------------

// An apple synth pedal
// --------------------------
//  (A)      | (B)       |  (env)
//  (rskip)  | (metro)   |  (level)
//
//  (range)   | (metro on)   | (?) | (?) 
//
//  (momentary bypass)  | (tap tempo)
// --------------------------
// A = synth param 1
// B = synth param 2
// 

// A GLITCH PEDAL
// **description**: hold to glitch, release to stop.
// glitch is a 50-300ms grain, looking back at the last 10 seconds of audio. 
// duration also has a metro that can trigger (or not) the glitch, as dictated by rskip prob. 
// spread scans through the 10s buffer to pick each grain
// --------------------------
// (glitch duration) | (spread) | (pitch/[SHIFT]pitch spread)
// (rskip)           | (level/[SHIFT]pattern length) | (filter/[SHIFT]crush amt) 
//
// (SHIFT) | (crush) | (random:pattern) | (atk-hard:atk-soft)
//
// (hold to record glitch) |  (tap tempo?---hold for hold)
// 
// extra: hold fsw2 THEN fsw1 to enter settings. there you can change the pattern length? 
// --------------------------
//


// LAYERCAKE: granular layers
// --------------------------
// *(duration/[SHIFT]rand dur amount)* | *(env/[SHIFT]direction)* | *(pitch/[SHIFT]rand pitch amt)* 
// *(metro/[SHIFT]trig sens)*                           | *(rskip/[SHIFT]spread)*  | *(level/[SHIFT]pattern length)*
//
// (SHIFT) | (METRO on/off) | (pitch:steps-octaves) | (random:pattern)
//
// sw(rec/play/dub/clear) | sw(?) 
// --------------------------


namespace daisysp
{

class BufView 
{
public:
    BufView() {}
    ~BufView() {}

    void init(float* buffer, size_t buf_frames, size_t buf_chans) {
        buf_ = buffer;
        frames_ = buf_frames;
        chans_ = buf_chans;

        assert(buf_ != nullptr);
    }

    float at(size_t frame_idx, size_t chan_idx) const {
        assert(frame_idx < frames_);
        assert(chan_idx < chans_);
        return buf_[frame_idx * chans_ + chan_idx];
    }

    float *get() {
        return buf_;
    }

    size_t frames() const {
        return frames_;
    }

    size_t chans() const {
        return chans_;
    }

private:
    float *buf_;
    size_t frames_;
    size_t chans_;  
};

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
        buf_.init(buffer, buf_frames, buf_chans);
        peeker_.Init(buf_.get(), buf_.frames(), buf_.chans());
        env_.Init(sr_);

        pos_ = 0.f;
        rate_st_ = 0.f;
        dur_ms_ = 80.f;

        state_ = State::IDLE;
    }

    State state() const {
        return state_;
    }

    float progress() const {
        return progress_;
    }

    // trigger the grain. 
    // pos_samples is the position in samples to start the grain from
    // rate_st is the pitch shift in semitones
    // dur_ms is the duration of the grain in milliseconds
    // env_atk is the attack time in seconds (default 0.01s)
    void Trigger(float pos_samples, float rate_st, float dur_ms, float env_atk = 0.01f) {
        pos_samples = fclamp(pos_samples, 0.f, (float)buf_.frames() - 1.f); // clamp to buffer size
        // truncate dur_ms if it exceeds the buffer size
        dur_ms = fclamp(dur_ms, 5.f, (float)buf_.frames() * 1000.f / sr_); // min 10ms, max buffer size in ms
        if ((pos_samples + (dur_ms * sr_ * 0.001f)) > (float)buf_.frames()) {
            dur_ms = ((float)buf_.frames() - pos_samples) / sr_ * 1000.f; // adjust duration to fit in buffer
        }

        pos_ = pos_samples;
        rate_st_ = rate_st;
        dur_ms_ = dur_ms;
        env_atk_ = env_atk;
        start_pos_ = pos_samples;
        end_pos_ = pos_samples + (dur_ms * sr_ * 0.001f); // end position in samples
        progress_ = 0.f; // reset progress

        float atk_time = fclamp(env_atk_ * dur_ms_, 2.f, dur_ms_); // min 10ms atk
        float decay_time = fclamp(dur_ms_ - atk_time, 2.f, dur_ms_); // min 5ms decay

        env_.SetTime(ADENV_SEG_ATTACK, atk_time * 0.001f);
        env_.SetTime(ADENV_SEG_DECAY, decay_time * 0.001f);
        env_.SetMin(0.f);
        env_.SetMax(1.f);
        env_.Trigger();

        state_ = State::PLAYING;
    }

    void ProcessOneFrame(BufView &out) {
        // Process the input buffer and produce output
        if (state_ == State::IDLE) {
            // do nothing
            for (size_t chan = 0; chan < buf_.chans(); ++chan) {
                out.get()[chan] = 0.f;
            }
        } else if (state_ == State::PLAYING) {
            // get envelope value
            float env_val = env_.Process();
            
            // read from the buf
            peeker_.Peek(pos_, out.get());

            // apply the envelope to the output
            for (size_t chan = 0; chan < buf_.chans(); ++chan) {
                out.get()[chan] *= env_val; // apply envelope
            }
        
            // calculate the increment based on the rate
            float inc = powf(2.f, rate_st_ / 12.0f);
            pos_ += inc;

            // update progress
            progress_ = (pos_ - start_pos_) / (end_pos_ - start_pos_);

            // if the env is done, stop the grain
            if (!env_.IsRunning()) {
                state_ = State::IDLE;
            }

            if (pos_ > ((float)buf_.frames() - 1)) {
                pos_ = 0.f; // wrap around
                state_ = State::IDLE; // stop the grain
            } 
        }
    }

private: 
    State state_;
    AdEnv env_;

    float sr_;
    BufView buf_;

    Ipeek peeker_;
    
    // playhead
    float pos_;
    
    // direction
    float direction_ = 1.f; // 1 for forward, -1 for backward

    float start_pos_ = 0.f; // start position in samples
    float end_pos_ = 0.f; // end position in samples
    float progress_ = 0.f; // progress in the grain

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
        buf_.init(buffer, buf_frames, buf_chans);
        for (auto &g : grains_) {
            g.Init(sr_, buf_.get(), buf_.frames(), buf_.chans());
        }
        grain_queue_.clear(); // clear the queue
    }

    void TriggerGrain(float pos_samples, 
                      float rate_st, 
                      float dur_ms, 
                      float env_atk = 0.01f, 
                      bool steal = true) {
        // default to the first grain, which 

        // find an idle grain and trigger it
        for (auto &g : grains_) {
            if (g.state() == Grain::State::IDLE) {
                g.Trigger(pos_samples, rate_st, dur_ms, env_atk);
                return; // only trigger one grain at a time
            }
        }
        // if no idle grain found, steal the last triggered grain
        if (steal) {
            grains_.back().Trigger(pos_samples, rate_st, dur_ms, env_atk);
        }

    }

    void ProcessOneFrame(BufView &out) {
        // Process all grains and produce output
        for (auto &g : grains_) {
            g.ProcessOneFrame(out);
        }
    }


private:
    float sr_;
    BufView buf_;
    std::array<Grain, 8> grains_; // array of grains, can be adjusted
    lifo_queue<size_t> grain_queue_; // queue for grains
};

} // namespace daisysp

#endif // __cplusplus
#endif // HUGO_LIB_GRAINS_H
