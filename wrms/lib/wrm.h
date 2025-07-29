#pragma once
#ifndef FE_WRM_H
#define FE_WRM_H

#ifdef __cplusplus


#include "daisysp.h"
#include "ipoke.h"
#include "hphasor.h"

namespace daisysp
{

class WrmsLooper 
{
public:
    WrmsLooper() {}
    ~WrmsLooper() {}

    enum class State { IDLE, RECORD, PLAYBACK, OVERDUB };
    
    void Init(float sample_rate, float* buffer, size_t buffer_size)
    {
        sr_ = sample_rate;
        buffer_.Init(sample_rate, buffer, buffer_size);
        buffer_.Fill(0.f); // fill buffer with zeros
        rate_st_line_.Init(sample_rate);

        pos_phasor_.Init(sample_rate);
        pos_phasor_.SetFreq(1.f * sr_ / buffer_size); // set initial frequency

        state_ = State::IDLE;
        loop_end_ = buffer_size;

        SetRateSemitones(0.0f); // start with 0 semitones
        SetRateSlewMs(100.f); // default slew time
        SetOverdub(0.5f); // no overdub by default
        SetLevel(1.f); // default level is 1.0
        Reset();
    }

    void SetState(State new_state){
        // if the new state is the same as the current state, do nothing
        if (new_state == state_) {
            return;
        }
        // if we're going from anywhere to idle, set freq to 0 and reset position
        if (new_state == State::IDLE) {
            pos_phasor_.SetPhase(0.f); // reset phase to 0
            SetRateSemitones(0.f); // reset rate to 0 semitones
        } else {
        }
        state_ = new_state;
    }

    State GetState() const {
        return state_;
    }

    void SetPhase(float pos) {
        // set the phase of the position phasor
        pos_phasor_.SetPhase(pos);
    }

    // set the end point of the loop
    // pos: [0, 1] where 0 is the start of the buffer and 1 is the end
    void SetLoopPoints(float start_phase, float end_phase) {
        float cur_index = pos_ * buffer_.GetRegionSize();
        
        buffer_.SetStartPoint(start_phase); // set the start point in the buffer
        buffer_.SetEndPoint(end_phase); // set the end point in the buffer

        float start_index = start_phase * buffer_.GetBufferSize();
        float end_index = end_phase * buffer_.GetBufferSize();
        
        // adjust the phasor to the new end point
        float new_index = fclamp(
            cur_index, 
            start_index, 
            end_index
        );
        // if (start_phase, end_phase) is the new (0, 1) for the buffer, 
        // adjust the position phasor to the new index according to the new range
        float new_pos = new_index / (end_index - start_index);
        pos_phasor_.SetPhase(new_pos); // set the phase of the position ph
    }

    float GetPhasorFreq() {
        // configure the position phasor
        float rate = pow(2, rate_st_ * kOneTwelfth); // convert semitones to frequency ratio
        return rate * sr_ / buffer_.GetRegionSize();
    }

    float Process(float in){
        uint8_t rate_st_line_finished = 0;
        rate_st_ = rate_st_line_.Process(&rate_st_line_finished);

        pos_phasor_.SetFreq(GetPhasorFreq()); // convert rate to frequency
        // pos_phasor.SetFreq(1)
        uint8_t pos_phasor_finished = 0;
        pos_ = pos_phasor_.Process(&pos_phasor_finished);

        // read from buffer
        float read_val = buffer_.Peek(pos_);

        if (state_ == State::RECORD && pos_phasor_finished) {
            SetState(State::PLAYBACK); // switch to playback when the phasor completes a cycle
        }

        // write to buf
        if (state_ == State::RECORD) {
            buffer_.IPoke(in, pos_);
        } else if (state_ == State::OVERDUB) {
            float write_val = in + read_val * overdub_;
            buffer_.IPoke(write_val, pos_);
        }

        // output the read value, advance the playhead
        if (state_ != State::IDLE && state_ != State::RECORD) {
            return read_val * level_;
        } else {
            return 0.f;
        }
        
    }

    float GetPosition()  {
        return pos_;
    }


    void SetOverdub(float overdub) {
        // overdub_ = fclamp(overdub, 0.f, 1.f);
        overdub_ = fmin(fmax(overdub, 0.f), 1.f); // clamp between 0 and 1
    }

    void SetRateSemitones(float target_rate_semitones) {
        rate_st_line_.Start(rate_st_, target_rate_semitones, rate_slew_ms_ * 0.001f);
    }

    float GetRateSemitones() {
        return rate_st_;
    }

    void SetRateSlewMs(float slew_ms) {
        rate_slew_ms_ = fmax(20.f, slew_ms);    
    }

    void SetLevel(float level) {
        level_ = fclamp(level, 0.f, 1.f);
    }

    void Reset() {
        SetState(State::IDLE);

        SetRateSemitones(0.f);
        SetRateSlewMs(100.f); // default slew time

        SetLoopPoints(0.0f, 1.0f); // reset loop points
    }
    
private:
    State state_ = State::IDLE;
    IpokeBuffer buffer_;
    
    float sr_;
    float loop_end_ = 1.0f;
    float overdub_ = 0.f;
    float level_ = 1.f; // output level

    HPhasor pos_phasor_;
    float pos_ = 0.f; // current position in the buffer
    
    Line rate_st_line_; // line for smoothing the rate
    float rate_slew_ms_ = 100.f; // slew time for rate changes in milliseconds
    float rate_st_ = 0.f; // playback rate in semitones    
};

} // namespace daisysp

#endif // __cplusplus
#endif // FE_WRM_H
