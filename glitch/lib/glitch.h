#pragma once
#ifndef HUGO_LIB_GRAINS_H
#define HUGO_LIB_GRAINS_H

#ifdef __cplusplus

#include "daisysp.h"
#include "ipoke.h"
#include "stack.h"
#include <array>

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
// (glitch duration/[SHIFT]rand dur amt) | (spread) | (pitch/[SHIFT]pitch spread)
// (rskip/[SHIFT]filter)           | (level/[SHIFT]pattern length) | (env/[SHIFT]overlap ) 
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
// *(metro/[SHIFT]trig sens)*  | *(rskip/[SHIFT]spread)*  | *(level/[SHIFT]pattern length)*
//
// (SHIFT) | (METRO on/off) | (pitch:steps-octaves) | (random:pattern)
//
// sw(rec/play/dub/clear) | sw(?) 
// --------------------------


namespace daisysp
{

inline float randf(float min, float max) {
    return min + (max - min) * (static_cast<float>(rand()) / RAND_MAX);
}

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

class GlitchEngine 
{
public:
    GlitchEngine() {}
    ~GlitchEngine() {}

    void Init(float sample_rate, 
              float* buffer, 
              size_t buf_frames, 
              size_t buf_chans) {
        sr_ = sample_rate;
        assert(buffer != nullptr); // make sure the buffer is not null
        buf_ = buffer;
        frames_ = buf_frames;
        chans_ = buf_chans;

        poker_.Init(buf_, buf_frames, buf_chans);
        poker_.SetOverdub(0.0f);
        grains_.Init(sr_, buffer, buf_frames, buf_chans);
        clock_.Init(1.f / (glitch_dur_ * 0.001f), sr_);

        std::fill(buf_, buf_ + (frames_ * chans_), 0.f);
    }

    float WrapPos(float pos) {
        while (pos < 0.f) pos += frames_;
        while (pos >= frames_) pos -= frames_;
        return pos;
    }

    void TriggerGlitch() {
        // lock the current pos as the glitch start position
        glitch_start_pos_ = WrapPos(wpos_ - (glitch_dur_ * sr_ * 0.001f));

        // configure the metro, trigger the metro
        clock_.Reset();

        enabled_ = true; // enable the glitch engine
        just_triggered_ = true; // set the flag to true

    }

    void StopGlitch() {
        enabled_ = false; // don't allow any more glitches
    }


    void ProcessOneFrame(const float *in, float *out) {
        // zero the output buffer
        for (size_t chan = 0; chan < chans_; ++chan) {
            out[chan] = 0.f;
        }

        // always record into the buffer
        // stop poking if we are enabled
        poker_.Poke(
            /*index=*/ enabled_ ? -1.f : wpos_,
            /*in=*/ in
        );

        // increment the write pos
        wpos_ += 1.f; // increment by one sample
        wpos_ = WrapPos(wpos_); // wrap around if needed

        // // check the clock to see if we need to trigger a glitch
        if ((clock_.Process()) && enabled_) {
            // start a new grain
            

            // float rate_st = pitch_ + (randf(-pitch_spread_, pitch_spread_));
            float rate_st = pitch_;
            if (pitch_spread_type_ == PitchSpreadType::PITCH_SPREAD_NONE) {
                // no spread
                rate_st = pitch_;
            } else if (pitch_spread_type_ == PitchSpreadType::PITCH_SPREAD_RAND) {
                // random spread
                rate_st = pitch_ + (randf(-pitch_spread_, pitch_spread_));
            } else if (pitch_spread_type_ == PitchSpreadType::PITCH_SPREAD_OCTAVES) {
                // octave spread
                int octaves = static_cast<int>(pitch_spread_ / 12.f);
                int step = (rand() % (2 * octaves + 1)) - octaves; // random step between -octaves and +octaves
                rate_st = pitch_ + (step * 12); // each octave is 12 semitones
            }

            float start_pos = WrapPos(glitch_start_pos_ + (randf(-spread_, 0.f) * frames_));
            // skip if we need to
            if ((randf(0.f, 1.f) < rskip_) && !just_triggered_) {
                // skip this grain
            } else {
                just_triggered_ = false;
                grains_.TriggerGrain(
                    /*pos_samples=*/ start_pos, 
                    /*rate_st=*/ rate_st,
                    /*dur_ms=*/ glitch_dur_,
                    /*env_atk=*/ env_atk_amt_,
                    /*steal=*/ true
                );
            }
        }

        // process the grains
        grains_.ProcessOneFrame(out);

        // apply the level to the output
        for (size_t chan = 0; chan < chans_; ++chan) {
            out[chan] *= level_; // apply the level
        }
    }

    void SetGlitchParams(float glitch_dur, float rskip, float spread, 
                         float pitch, float pitch_spread, float level, 
                         float env_atk_amt) {
        glitch_dur_ = fclamp(glitch_dur, 20.f, 5000.f); // clamp between 20ms and 2000ms
        rskip_ = fclamp(rskip, 0.f, 1.f); // clamp between 0 and 1
        spread_ = fclamp(spread, 0.f, 1.f); // clamp between 0 and 1
        pitch_ = pitch; // set the pitch
        pitch_spread_ = fclamp(pitch_spread, 0.f, 12.f); // clamp between 0 and 12 semitones
        level_ = fclamp(level, 0.f, 1.f); // clamp between 0 and 1
        env_atk_amt_ = fclamp(env_atk_amt, 0.f, 1.f); // clamp between 0 and 1

        clock_.SetFreq(1.f / (glitch_dur_ * 0.001f)); // update the clock frequency
    }

    void PrintDebugState(daisy::DaisyPetal &hw){
        hw.seed.PrintLine("Glitch Engine State:");
        hw.seed.PrintLine("  Sample Rate: %f", sr_);
        hw.seed.PrintLine("  Buffer Frames: %d", frames_);
        hw.seed.PrintLine("  Write Position: %f", wpos_);
        hw.seed.PrintLine("  Enabled: %d", enabled_);
        hw.seed.PrintLine("  Glitch Start Position: %f", glitch_start_pos_);
        hw.seed.PrintLine("  Glitch Duration: %f", glitch_dur_);
        hw.seed.PrintLine("  Rskip Probability: %f", rskip_);
        hw.seed.PrintLine("  Spread: %f", spread_);
        hw.seed.PrintLine("  Pitch: %f", pitch_);
        hw.seed.PrintLine("  Pitch Spread: %f", pitch_spread_);
        hw.seed.PrintLine("  Level: %f", level_);
        hw.seed.PrintLine("  Envelope Attack Amount: %f", env_atk_amt_);
        hw.seed.PrintLine("  Just Triggered: %d", just_triggered_);
        hw.seed.PrintLine("  Grains:");
        // print grains in a table to avoid clutter
        hw.seed.PrintLine("  State | Start Pos | End Pos | Rate (st) | Duration (ms) | Env Atk");
        hw.seed.PrintLine("  ----- | ---------- | -------- | --------- | ------------- | ---------");
        for (size_t i = 0; i < grains_.grains_.size(); ++i) {
            const auto& grain = grains_.grains_[i];
            grains_.grains_[i].PrintDebugState(hw);
            hw.seed.PrintLine(" ");
        }
    }

    Metro & clock() {
        return clock_;
    }

    enum class PitchSpreadType {
        PITCH_SPREAD_NONE = 0,
        PITCH_SPREAD_RAND,
        PITCH_SPREAD_OCTAVES,
    };

    void SetPitchSpreadType(PitchSpreadType type) {
        pitch_spread_type_ = type;
    }
private:
    float sr_;
    float *buf_;
    size_t frames_;
    size_t chans_;

    Ipoke poker_;
    Grains grains_; // grains for glitching
    Metro clock_; // grain clock

    float wpos_ = 0.f; // write position in the buffer
    bool enabled_ = true; // whether the glitch engine is enabled

    // params
    float glitch_start_pos_ = 0.f; // start position in samples of the triggered glitch. 
    float glitch_dur_ = 80.f; // duration of the glitch in milliseconds
    float rskip_ = 0.3f; // probability of skipping a grain
    float spread_ = 0.; // spread of each grain trigger position (0-1.f)
    float pitch_ = 0.f; // pitch shift in semitones
    float pitch_spread_ = 0.f; // pitch spread (+/-) in semitones
    PitchSpreadType pitch_spread_type_ = PitchSpreadType::PITCH_SPREAD_RAND;
    float level_ = 1.f; // output level
    float env_atk_amt_ = 0.1f; // attack proportion for the envelope (0.-1.f)

    bool just_triggered_ = false; // whether the glitch was just triggered

};

} // namespace daisysp

#endif // __cplusplus
#endif // HUGO_LIB_GRAINS_H
