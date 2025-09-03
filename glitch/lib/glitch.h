#pragma once
#ifndef HUGO_LIB_GLITCH_H
#define HUGO_LIB_GLITCH_H

#ifdef __cplusplus

#include "fmath.h"
#include "grain.h"
#include "daisysp.h"
#include "ipoke.h"
#include <array>
#include "window.h"

// A GLITCH PEDAL
// **description**: hold to glitch, release to stop.
// glitch is a 50-300ms grain, looking back at the last 10 seconds of audio. 
// duration also has a metro that can trigger (or not) the glitch, as dictated by rskip prob. 
// spread scans through the 10s buffer to pick each grain
// --------------------------
// (glitch duration/[SHIFT]rand dur amt) | (spread/[SHIFT]rand) | (pitch/[SHIFT]pitch spread)
// (pattern/[SHIFT]rskip)           | (level/[SHIFT]) | (env/[SHIFT]overlap ) 
//
// () | () | (oct/step) | ()
//
// (press/hold to glitch) |  (tap tempo?---hold for SHIFT)
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


struct GrainEvent {
    float pos_samples;
    float rate_st;
    float dur_ms;
    float env_atk;
    bool skipped;
    bool fwd;
};

class GrainPattern {

public:
    GrainPattern() {}
    ~GrainPattern() {}

    void Init(size_t max_pattern_length = 16) {
        max_pattern_length_ = max_pattern_length;
        pattern_.reserve(max_pattern_length_);
        Reset();
    }

    void Reset() {
        pattern_.clear();
        pattern_idx_ = 0;
    } 

    // process a grain event, 
    // either replacing it with a previously stored pattern 
    // or simply passing it throug
    void ProcessEvent(GrainEvent &event) {
        if (pattern_.size() >= pattern_length_) { 
            // we have recorded enough events, just play them back
            event = pattern_[pattern_idx_];
        } else {
            // we have not recorded enough events, just pass it through and record it
            pattern_.push_back(event);
        }

        // inc and wrap pattern index
        pattern_idx_++;
        if (pattern_idx_ >= pattern_length_) {
            pattern_idx_ = 0;
        }
    }

    void SetPatternLength(size_t length) {
        length = (length < 1) ? 1 : length;
        pattern_length_ = length;
    }

    size_t GetPatternLength() const {
        return pattern_length_;
    }

    size_t GetPatternIndex() const {
        return pattern_idx_;
    }

private:
    size_t max_pattern_length_ = 16;
    size_t pattern_length_ = 8;
    size_t pattern_idx_ = 0;
    std::vector<GrainEvent> pattern_;
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
        pattern_.Init(16);
        clock_.Init(1.f / (glitch_dur_ * 0.001f), sr_);

        window_.Init(sr_);
        window_.BeginFadeIn(kWindowFadeMs);

        std::fill(buf_, buf_ + (frames_ * chans_), 0.f);
        sig_.assign(1 * chans_, 0.f); // a single frame buffer.
    }

    float WrapPos(float pos) {
        while (pos < 0.f) {
            pos += frames_;
        }
        while (pos >= frames_) {
            pos -= frames_;
            poker_.ResetIndex();
        }
        return pos;
    }

    void TriggerGlitch() {
        // configure the metro, trigger the metro
        clock_.Reset();
        clock_idx_ = 0;

        enabled_ = true; // enable the glitch engine
        just_triggered_ = true; // set the flag to true

    }

    void StopGlitch() {
        enabled_ = false; // don't allow any more glitches
    }

    void ProcessFrame(const float *in, float *out) {
        // zero the output buffer
        for (size_t chan = 0; chan < chans_; ++chan) {
            out[chan] = 0.f;
        }

        // check if we should write to the buffer
        bool should_write = !(freeze_ && enabled_ && clock_idx_ > 0);
        if (should_write && !last_should_write_) {
            // we just started writing
            window_.BeginFadeIn(kWindowFadeMs);
        } else if (!should_write && last_should_write_) {
            // we just stopped writing
            window_.BeginFadeOut(kWindowFadeMs);
        }
        last_should_write_ = should_write;

        // apply window to input
        float win = window_.ProcessFrame(); // get the window value
        for (size_t chan = 0; chan < chans_; ++chan) {
            sig_[chan] = in[chan] * win;
        }

        // always record into the buffer
        // stop poking if we are enabled
        bool window_off = (window_.IsOff());
        poker_.Poke(
            /*index=*/ window_off ? -1.f : wpos_,
            /*in=*/ sig_.data()
        );

        // increment the write pos
        if (!window_off) {
            wpos_ += 1.f; // increment by one sample
            wpos_ = WrapPos(wpos_); // wrap around if needed
        }

        // // check the clock to see if we need to trigger a glitch
        uint8_t clock_tick = clock_.Process();
        if (clock_tick) clock_idx_++; // increment the clock index

        // don't begin until clock index is 1, 
        // this way we "record" the glitch during the first clock tick. 
        bool should_trigger = clock_tick && enabled_ && clock_idx_ > 0;
        if (should_trigger) {
            // start a new grain

            // CALCULATE GRAIN EVENT PARAMS
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
            float duration = glitch_dur_ * overlap_; // duration of the glitch in milliseconds

            // if the rate is > 1, we need to start earlier to avoid going out of bounds
            float rate = powf(2, rate_st / 12.f);
            
            // BEGIN CALCULATE start_pos
            // adjust start position depending on the rate we sampled
            if (rate > 1.f || overlap_ > 1.f) {
                // now, move back by the amount we will play during the grain
                // `duration` already takes overlap into account, so we just need to account for rate
                glitch_start_pos_ = WrapPos(wpos_ - ((duration * 0.001f) * sr_ * rate));
            }
            // apply spread to the start position // (only to the past as to not go out of bounds)
            float start_pos = glitch_start_pos_;
            start_pos = WrapPos(start_pos - (frames_ * mem_) + (randf(-spread_, 0.f) * frames_ * mem_));
            // END CALCULATE start_pos

            // decide if we should skip this grain based on rskip probability
            bool skip = (randf(0.f, 1.f) < rskip_) && !just_triggered_;

            // create a default grain event, 
            // this may be replaced if pattern is playing
            GrainEvent event = { 
                .pos_samples = start_pos, 
                .rate_st = rate_st, 
                .dur_ms = duration, 
                .env_atk = env_atk_amt_,
                .skipped = skip};
            if (pattern_mode_) {
                pattern_.ProcessEvent(event); // process the event through the pattern
            }
            // skip if we need to
            if (event.skipped) {
                // skip this grain
            } else {
                just_triggered_ = false;
                grains_.TriggerGrain(
                    /*pos_samples=*/ event.pos_samples, // always override start pos 
                    /*rate_st=*/ event.rate_st,
                    /*dur_ms=*/ event.dur_ms,
                    /*env_atk=*/ event.env_atk,
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

    void SetPatternLength(size_t length) {
        if (length < 1) {
            pattern_mode_ = false;
        } else {
            pattern_mode_ = true;
            pattern_.SetPatternLength(length);
        }
    }

    void ResetPattern() {
        pattern_.Reset();
    }

    // set glitch memory (how far to look back in time during playback)
    void SetGlitchMemory(float mem) {
        mem_ = fclamp(mem, 0.0, 1.0);
    }


    void SetGlitchParams(float glitch_dur, float rskip, float spread, 
                         float pitch, float pitch_spread, float level, 
                         float env_atk_amt, bool freeze, float overlap) {
        glitch_dur_ = fclamp(glitch_dur, 20.f, 5000.f); // clamp between 20ms and 2000ms
        rskip_ = fclamp(rskip, 0.f, 1.f); // clamp between 0 and 1
        spread_ = fclamp(spread, 0.f, 1.f); // clamp between 0 and 1
        pitch_ = pitch; // set the pitch
        pitch_spread_ = fclamp(pitch_spread, 0.f, 12.f); // clamp between 0 and 12 semitones
        level_ = fclamp(level, 0.f, 1.f); // clamp between 0 and 1
        env_atk_amt_ = fclamp(env_atk_amt, 0.f, 1.f); // clamp between 0 and 1
        freeze_ = freeze; // set the freeze state
        overlap_ = fclamp(overlap, 0.1f, 4.f); // clamp between 0.1 and 4

        clock_.SetFreq(1.f / (glitch_dur_ * 0.001f)); // update the clock frequency
    }

    void PrintDebugState(daisy::DaisyPetal &hw){
        hw.seed.PrintLine("Glitch Engine State:");
        // hw.seed.PrintLine("  Sample Rate: %f", sr_);        // hw.seed.PrintLine("  Buffer Frames: %d", frames_);
        hw.seed.PrintLine("  Write Position: %f", wpos_);
        hw.seed.PrintLine("  Enabled: %d", enabled_);
        hw.seed.PrintLine("  Glitch Start Position: %f", glitch_start_pos_);
        hw.seed.PrintLine("  Glitch Duration: %f", glitch_dur_);
        // hw.seed.PrintLine("  Rskip Probability: %f", rskip_);
        hw.seed.PrintLine("  Spread: %f", spread_);
        hw.seed.PrintLine("  Pitch: %f", pitch_);
        hw.seed.PrintLine("  Pitch Spread: %f", pitch_spread_);
        hw.seed.PrintLine("  Level: %f", level_);
        hw.seed.PrintLine("  Envelope Attack Amount: %f", env_atk_amt_);
        hw.seed.PrintLine("  Just Triggered: %d", just_triggered_);
        hw.seed.PrintLine("  Grains:");
        hw.seed.PrintLine("  clock idx: %d", clock_idx_);
        hw.seed.PrintLine("  Last Should Write: %d", last_should_write_);
        hw.seed.PrintLine("  Window State: %s", 
            window_.IsOn() ? "On" : 
            window_.IsOff() ? "Off" : 
            window_.IsFadingIn() ? "Fading In" :
            window_.IsFadingOut() ? "Fading Out" : "Unknown"
        );
        hw.seed.PrintLine("  Pattern Mode: %d", pattern_mode_);
        hw.seed.PrintLine("  Pattern Length: %d", pattern_.GetPatternLength());
        hw.seed.PrintLine("  Pattern Index: %d", pattern_.GetPatternIndex());

        // print the debug state of each grain
        hw.seed.PrintLine("  Grains:");
        grains_.PrintDebugState(hw);
        hw.seed.PrintLine("  ");
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
        if (type != pitch_spread_type_) {
            pitch_spread_type_ = type;
            // reset pattern
            ResetPattern();
        }
    }

private:
    float sr_;
    float *buf_;
    size_t frames_;
    size_t chans_;

    float mem_; // 0.0->1.0 -- how far to look back, aka "scan"
    float rand_direction_; // random direction: 0.0 - all fwd, 0.5 - fwd/rev, 1.0 - all rev

    std::vector<float> sig_; // signal buffer for processing

    Ipoke poker_;
    Grains grains_; // grains for glitching
    Metro clock_; // grain clock
    size_t clock_idx_ = 0;

    float wpos_ = 0.f; // write position in the buffer
    bool enabled_ = true; // whether the glitch engine is enabled

    bool last_should_write_ = false; // whether we wrote to the buffer last time

    GrainPattern pattern_;
    bool pattern_mode_ = false; // whether the pattern mode is enabled

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
    bool freeze_ = false; // whether to freeze the buffer on trigger. 
    float overlap_ = 0.f; // overlap amount (0-1.f) for the grains, not used yet

    bool just_triggered_ = false; // whether the glitch was just triggered

    Window window_;
    static constexpr float kWindowFadeMs = 50.f; // fade in the window over 50ms
}; 

} // namespace daisysp

#endif // __cplusplus
#endif // HUGO_LIB_GLITCH_H
