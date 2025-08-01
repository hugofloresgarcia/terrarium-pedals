
#pragma once

#ifndef H_CONTROL_RECORDER_H
#define H_CONTROL_RECORDER_H

#include <cmath>
#include <cstdint>
#include "daisy_seed.h"
#include "daisysp.h"
#include "state.h"

using namespace daisy;
using namespace daisysp;

namespace daisysp
{
class TerrariumControlRecorder {
public:
    TerrariumControlRecorder() {}
    ~TerrariumControlRecorder() {}

    enum class CtrlRecorderState {
        IDLE,
        RECORDING,
        PLAYING
    };

    void Init()
    {
        index_ = 0;
        for (int i = 0; i < n_pots_; i++) {
            prev_pots_[i] = 0.0f;
            pot_override_[i] = false;
        }
        for (int i = 0; i < n_switches_; i++) {
            prev_switches_[i] = false;
            switch_override_[i] = false;
        }
    }

    void StartRecording()
    {
        // index_ = 0;
        if (state_ != CtrlRecorderState::RECORDING) {
            index_ = 0;
        }
        state_ = CtrlRecorderState::RECORDING;
        ResetOverrides();
    }

    void StartPlaying()
    {
        index_ = 0;
        state_ = CtrlRecorderState::PLAYING;
        ResetOverrides();
    }

    void StopPlaying()
    {
        index_ = 0;
        state_ = CtrlRecorderState::IDLE;
        ResetOverrides();
    }

    void SetListenForOverrides(bool listen)
    {
        ResetOverrides();
        listen_for_overrides_ = listen;
    }

    CtrlRecorderState GetState() const { return state_; }

    size_t GetIndex()
    {
        return index_;
    }

    // void Process(float* pots, bool* switches)
    void Process(
        TerrariumState& s
    )
    {
        float pots[n_pots_];
        bool switches[n_switches_];

        pots[0] = s.pot1;
        pots[1] = s.pot2;
        pots[2] = s.pot3;
        pots[3] = s.pot4;
        pots[4] = s.pot5;
        pots[5] = s.pot6;

        switches[0] = s.sw1;
        switches[1] = s.sw2;
        switches[2] = s.sw3;
        switches[3] = s.sw4;

        if (state_ == CtrlRecorderState::RECORDING) {
            // Record
            for (uint8_t i = 0; i < n_pots_; i++) {
                pot_buf_[index_][i] = pots[i];
            }
            for (uint8_t i = 0; i < n_switches_; i++) {
                switch_buf_[index_][i] = switches[i];
            }

            last_recorded_index_ = index_;

            if (++index_ >= buf_size_) {
                StartPlaying(); 
                ResetOverrides();
            }
        }
        else if (state_ == CtrlRecorderState::PLAYING) {

            if (listen_for_overrides_) {
                // Detect overrides (same as before)
                for (uint8_t i = 0; i < n_pots_; i++) {
                    if (!pot_override_[i] && fabs(pots[i] - prev_pots_[i]) > 0.02f) {
                        pot_override_[i] = true;
                    }
                }
                for (uint8_t i = 0; i < n_switches_; i++) {
                    if (!switch_override_[i] && switches[i] != prev_switches_[i]) {
                        switch_override_[i] = true;
                    }
                }
            }
            // Save current hardware state for next frame comparison
            for (uint8_t i = 0; i < n_pots_; i++) 
                prev_pots_[i] = pots[i];
            for (uint8_t i = 0; i < n_switches_; i++) 
                prev_switches_[i] = switches[i];


            // Apply recorded values for non-overridden controls
            for (uint8_t i = 0; i < n_pots_; i++) {
                if (!pot_override_[i]) {
                    pots[i] = pot_buf_[index_][i];
                }
            }
            for (uint8_t i = 0; i < n_switches_; i++) {
                if (!switch_override_[i]) {
                    switches[i] = switch_buf_[index_][i];
                }
            }

            // Advance playback index
            index_++;
            if (index_ >= last_recorded_index_) {
                index_ = 0;
            }
        }

        s.pot1 = pots[0];
        s.pot2 = pots[1];
        s.pot3 = pots[2];
        s.pot4 = pots[3];
        s.pot5 = pots[4];
        s.pot6 = pots[5];
        s.sw1 = switches[0];
        s.sw2 = switches[1];
        s.sw3 = switches[2];
        s.sw4 = switches[3];
    }

private:
    void ResetOverrides()
    {
        for (int i = 0; i < n_pots_; i++) pot_override_[i] = false;
        for (int i = 0; i < n_switches_; i++) switch_override_[i] = false;
    }

    static constexpr uint8_t n_pots_ = 6;
    static constexpr uint8_t n_switches_ = 4;
    static constexpr uint16_t buf_size_ = 4000;
    size_t last_recorded_index_ = 0;

    float pot_buf_[buf_size_][n_pots_];
    bool switch_buf_[buf_size_][n_switches_];

    float prev_pots_[n_pots_];
    bool prev_switches_[n_switches_];

    bool pot_override_[n_pots_];
    bool switch_override_[n_switches_];

    bool listen_for_overrides_ = true; 


    size_t index_;
    CtrlRecorderState state_ = CtrlRecorderState::IDLE;
};


} // namespace daisysp



#endif // H_CONTROL_RECORDER_H
