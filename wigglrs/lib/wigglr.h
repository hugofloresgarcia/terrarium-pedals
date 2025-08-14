#pragma once
#ifndef WMRS_LIB_WIGGLR_H
#define WMRS_LIB_WIGGLR_H

#ifdef __cplusplus

#include "daisysp.h"
#include "ipoke.h"

namespace daisysp
{

class Wigglr 
{
public:
    Wigglr() {}
    ~Wigglr() {}

    enum class State
    {
        EMPTY,
        REC_FIRST, 
        PLAYING, 
        REC_DUB,
    };

    void Init(float sr, float *buf, size_t frames, size_t chans) {
        sr_ = sr;
        buf_ = buf;
        frames_ = frames;
        chans_ = chans;

        rate_st_line_.Init(sr);
        peeker_.Init(buf_, frames_, chans_);
        poker_.Init(buf_, frames_, chans_);
        state_ = State::EMPTY;

        // sig_ = new float[chans_]();
        sig_.assign((size_t)chans_, 0.0f);
        InitBuff();
    }

    void SetLevel(float level) {
        level_ = fclamp(level, 0.f, 1.f);
    }

    float GetRateSemitones() const {
        return rate_st_;
    }

    float GetTargetRateSemitones() const {
        return rate_st_line_.GetEnd();
    }

    void SetRateSemitones(float target_rate_semitones) {
        rate_st_line_.Start(rate_st_, target_rate_semitones, rate_slew_ms_ * 0.001f);
    }

    void SetRateSlewMs(float rate_slew_ms) {
        rate_slew_ms_ = rate_slew_ms;
    }

    void SetOverdub(float overdub) {
        overdub_ = fmin(fmax(overdub, 0.f), 1.f); // clamp between 0 and 1
    }

    void ProcessFrame(const float *in, float *out) {
        // figure out sample increment
        float inc = 1.;
        if (state_ == State::EMPTY || state_ == State::REC_FIRST) {
            inc = 1.;
        } else {
            uint8_t rate_st_line_finished = 0;
            rate_st_ = rate_st_line_.Process(&rate_st_line_finished);
            inc = powf(2, rate_st_ / 12.0f);
        }

        win_ = WindowVal(win_idx_ * kWindowFactor);

        if (state_ == State::EMPTY) {
            for (size_t chan = 0; chan < chans_; ++chan) {
                out[chan] = 0.0f;
                poker_.Poke(-1.f, sig_.data()); // stop writing

            }
        } else if (state_ == State::REC_FIRST) {
            for (size_t chan = 0; chan < chans_; ++chan) {
                out[chan] = 0.0f;
            }
            for (size_t chan = 0; chan < chans_ ; ++chan) {
                sig_[chan] = SoftLimit(in[chan] * win_);
            }
            poker_.SetOverdub(0.f);
            poker_.Poke(pos_, sig_.data());

            if (win_idx_ < kWindowSamps - 1) {
                win_idx_ += 1;
            }
            recsize_ = pos_;
            pos_    += inc;

            if (pos_ > ((float)frames_ - 1)) {
                state_   = State::PLAYING;
                pos_     = 0;
                // TODO: should we be resetting win idx here to 0? 
                win_idx_ = 0;
            }
        } else if (state_ == State::PLAYING) {
            peeker_.Peek(pos_, out);
            // "seamless looping: the first N samps after recording is done are recorded with the input faded out."

            if (win_idx_ < kWindowSamps - 1) {
                for (size_t chan = 0; chan < chans_ ; ++chan) {
                    sig_[chan] = out[chan] + in[chan] * (1.f - win_);
                } 
                poker_.SetOverdub(0.f);
                poker_.Poke(pos_, sig_.data());
                win_idx_ += 1;
            } else {
                poker_.SetOverdub(overdub_);
                poker_.Poke(-1.f, sig_.data()); // stop writing
            }

            pos_ += inc;
            if (pos_ > recsize_ - 1){
                pos_  = 0;
            } else if (pos_ < 0){
                pos_ = recsize_ - 1;
            }
        } else if (state_ == State::REC_DUB) {
            peeker_.Peek(pos_, out);

            poker_.SetOverdub(overdub_);

            for (size_t chan = 0; chan < chans_ ; ++chan) {
                sig_[chan] = SoftLimit(in[chan] * win_);
            } 
            poker_.Poke(pos_, sig_.data());

            // increment win idx and pos
            if (win_idx_ < kWindowSamps - 1) {
                win_idx_ += 1;
            }

            // handle pos
            pos_ += inc;
            if (pos_ > recsize_ - 1){
                pos_  = 0;
                poker_.ResetIndex(); // reset the index in the poker
            } else if (pos_ < 0){
                pos_ = recsize_ - 1;
            }
        }

        // apply level
        for (size_t chan = 0; chan < chans_; ++chan) {
            out[chan] = out[chan] * level_;
        }

        near_beginning_ = state_ != State::EMPTY && !Recording() && pos_ < 4800 ? true : false;
    }

    void SetPositionSamples(float pos) {
        if (pos < 0.f) {
            pos_ = 0.f;
        } else if (pos > recsize_ - 1) {
            pos_ = recsize_ - 1;
        } else {
            pos_ = pos;
        }
    }

    float GetPositionSamples() const {
        return pos_;
    }

    size_t GetRecSizeSamples() const {
        return recsize_;
    }

    inline void Clear() {
        state_ = State::EMPTY; 
    }

    inline const bool Recording() const { return state_ == State::REC_DUB || state_ == State::REC_FIRST; }

    inline bool IsNearBeginning() { return near_beginning_; }

    inline void TrigRecord() {
        switch (state_)
        {
            case State::EMPTY:
                pos_        = 0;
                recsize_    = 0;
                state_      = State::REC_FIRST;
                SetRateSemitones(0.f);
                break;
            case State::REC_FIRST: 
                pos_ = 0;
                state_ = State::PLAYING; 
                poker_.ResetIndex();
                break;
            case State::REC_DUB: 
                state_ = State::PLAYING; 
                break;
            case State::PLAYING: 
                poker_.ResetIndex();
                state_ = State::REC_DUB; 
                break;
            default: state_ = State::EMPTY; break;

        }
        win_idx_ = 0;
    }

    State GetState() const { return state_; }

public:// TODO: make private. just for debugging to print

    void InitBuff() {
        std::fill(&buf_[0], &buf_[frames_ * chans_], 0);
    }

    float WindowVal(float in) { return sin(HALFPI_F * in);}
    // float WindowVal(float in) { return 1.f;}

public: // TODO: make private. just for debugging to print

    State state_; 
    size_t recsize_; // size of the area being effectively recorded into. 
    
    float sr_;

    float *buf_;
    size_t frames_;
    size_t chans_;

    std::vector<float> sig_; // temp vector for output

    Ipeek peeker_;
    Ipoke poker_;

    // position, window val
    float pos_, win_;

    size_t win_idx_ = 0; // window index 

    // constants
    static constexpr float kWindowSamps = 1024;
    static constexpr float kWindowFactor = (1.f / kWindowSamps);

    float level_ = 1.f;
    float overdub_ = 0.f;

    Line rate_st_line_; // line for smoothing the rate
    float rate_slew_ms_ = 100.f; // slew time for rate changes in milliseconds
    float rate_st_ = 0.f; // playback rate in semitones    

    // bool rec_queue_; 
    bool near_beginning_ = false; // whether the position is near the beginning of the buffer

};

} // namespace daisysp

#endif // __cplusplus
#endif // WMRS_LIB_WIGGLR_H
