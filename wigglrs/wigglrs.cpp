#include "daisy_petal.h"
#include "daisysp.h"
#include "terrarium.h"
#include "lib/wigglr.h"

using namespace daisy;
using namespace daisysp;
using namespace terrarium;

// Declare a global daisy_petal for hardware access
DaisyPetal  hw;

bool        fsw1, fsw2, sw1, sw2, sw3, sw4;
bool        fsw1_momentary, fsw2_momentary = false; // for temporary bypass mode
Led         led1, led2;


Parameter   knob_wigglr1_vol, 
            knob_wigglrs_jumpamt, 
            knob_wigglr2_vol, 
            knob_wigglr_odb, 
            knob_wigglrs_slew, 
            knob_wigglr_skip;

float sr;

#define WIGGLR_BUF_SIZE (48000 * 60)  // 60 seconds of audio at 48kHz
#define WIGGLR_CHANS 1 // mono :(
#define BLOCK_SIZE 2 // 2 samples per block for audio processing

float DSY_SDRAM_BSS wigglr1_buf[WIGGLR_BUF_SIZE * WIGGLR_CHANS];
float DSY_SDRAM_BSS wigglr2_buf[WIGGLR_BUF_SIZE * WIGGLR_CHANS];

// intermediate buffers for wigglr output
float wigglr_in[WIGGLR_CHANS];
float wigglr1_out[WIGGLR_CHANS];
float wigglr2_out[WIGGLR_CHANS];

Wigglr wigglr1, wigglr2;

float fsw_held_ms = 300.f;
float max_slew_ms = 2000.f;

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
        is_blinking_ = true;
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
                blink_lfo_.SetFreq(16.0f); // faster blink for short blink effect
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
                if (blink_value < 0.5f) {led_.Set(0.5f);} // keep LED dimmed during blink
            }
            led_.Update(); // update the LED state
        }
        else {
            led_.Update(); // update the LED state when not blinking
        }
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


// a string to print if new messages were produced in the audio loop
LedWrap led1_wrap, led2_wrap;

Metro skip_metro;
Maytrig skip_maytrig;

void configure_worm(Wigglr &wigglr, float level, float overdub, 
    float rate_slew_ms, bool jump_up, bool jump_down, float jump_semitones, 
    bool footswitch_rising, bool footswitch_held, LedWrap &led_wrap, 
    uint8_t may_skip_trig, float skip_prob)
{
    // wigglrS CONFIG
    wigglr.SetLevel(level);
    wigglr.SetOverdub(overdub);
    wigglr.SetRateSlewMs(rate_slew_ms); // convert to milliseconds

    if (jump_down) {
        wigglr.SetRateSemitones(
            wigglr.GetTargetRateSemitones() - jump_semitones
        ); // decrease by jump_semitones
    }
    if (jump_up) {
        wigglr.SetRateSemitones(
            wigglr.GetTargetRateSemitones() + jump_semitones
        ); // increase by jump_semitones
    }

    if (footswitch_rising) {
        wigglr.TrigRecord();
    }

    if (footswitch_held)
    {
        wigglr.Clear();
    }

    if (wigglr.GetState() == Wigglr::State::REC_DUB || 
        wigglr.GetState() == Wigglr::State::REC_FIRST) {
        led_wrap.SetState(LedWrap::LedState::BLINKING);
    } else if (wigglr.GetState() == Wigglr::State::PLAYING) {
        // avoid overriding the short blink animation
        if (led_wrap.GetState() != LedWrap::LedState::BLINK_SHORT) {
            led_wrap.SetState(LedWrap::LedState::ON);
        }
    } else {
        led_wrap.SetState(LedWrap::LedState::OFF);
    }

    if (wigglr.GetState() == Wigglr::State::PLAYING) {
        if (may_skip_trig) {
            // skip prob: 
            // this control is split where 0.5 is 0% chance to skip. 
            // 0.5 -> 0.0 is 0% to 100% chance to skip WITHOUT random octave changes
            // 0.5 -> 1.0 is 0% to 100% chance to skip WITH random octave changes

            float actual_skip_prob  = skip_prob < 0.5f ? (0.5f - skip_prob) * 2.f : (skip_prob - 0.5f) * 2.f;
            bool octave_change_prob = skip_prob < 0.5f ?  0.0f                    : (skip_prob - 0.5f) * 2.f;
            bool skip = skip_maytrig.Process(actual_skip_prob);
            if (actual_skip_prob < 0.25f) {
                skip = false;
            } else {
                skip = skip_maytrig.Process(actual_skip_prob-0.24f);
            }
            if (skip) {
                // pick a random position in the buffer to skip to
                float pos = (float)(rand() % wigglr.GetRecSizeSamples());
                wigglr.SetPositionSamples(pos);
                led_wrap.SetState(LedWrap::LedState::BLINK_SHORT);

                bool skip_octave = skip_maytrig.Process(octave_change_prob);
                if (skip_octave) {
                    // randomly change the pitch by up to +/- 2 octaves
                    int octave_shift = (rand() % 5) - 2; // random int between -2 and +2
                    wigglr.SetRateSemitones(
                        0.f + (octave_shift * 12.f)
                    );
                }
            }
        }
    }
}

/*
 * Process terrarium knobs and switches
 */
void processTerrariumControls() {
    bool fsw1_rising = hw.switches[Terrarium::FOOTSWITCH_1].RisingEdge();
    bool fsw2_rising = hw.switches[Terrarium::FOOTSWITCH_2].RisingEdge();

    bool fsw1_held = (hw.switches[Terrarium::FOOTSWITCH_1].Pressed() && 
        hw.switches[Terrarium::FOOTSWITCH_1].TimeHeldMs() > fsw_held_ms);
    bool fsw2_held = (hw.switches[Terrarium::FOOTSWITCH_2].Pressed() && 
        hw.switches[Terrarium::FOOTSWITCH_2].TimeHeldMs() > fsw_held_ms);

    sw1 = hw.switches[Terrarium::SWITCH_1].Pressed();
    sw2 = hw.switches[Terrarium::SWITCH_2].Pressed();
    sw3 = hw.switches[Terrarium::SWITCH_3].Pressed();
    sw4 = hw.switches[Terrarium::SWITCH_4].Pressed();

    bool sw1_re = hw.switches[Terrarium::SWITCH_1].RisingEdge();
    bool sw2_re = hw.switches[Terrarium::SWITCH_2].RisingEdge();
    bool sw3_re = hw.switches[Terrarium::SWITCH_3].RisingEdge();
    bool sw4_re = hw.switches[Terrarium::SWITCH_4].RisingEdge();

    // update knob values
    // https://electro-smith.github.io/libDaisy/classdaisy_1_1_analog_control.html
    knob_wigglr1_vol .Process();
    knob_wigglrs_jumpamt .Process();
    knob_wigglr2_vol .Process();
    knob_wigglr_odb  .Process();
    knob_wigglrs_slew.Process();
    knob_wigglr_skip .Process();

    uint8_t may_skip_trig = skip_metro.Process();
    may_skip_trig = 0; // DISABLE SKIP!!
    float skip_prob = knob_wigglr_skip.Value();

    // map jumpamt to a the following discrete values = [1, 5, 7, 12]
    static constexpr float jump_semitones_map[] = {1.f, 5.f, 7.f, 12.f};
    size_t jump_semitones_idx = (size_t)(knob_wigglrs_jumpamt.Value() * 3.0f);
    float jump_semitones = jump_semitones_map[jump_semitones_idx];    

    configure_worm(
        /*wigglr=*/             wigglr1, 
        /*level=*/              knob_wigglr1_vol.Value(),
        /*overdub=*/            knob_wigglr_odb.Value(),
        /*rate_slew_ms=*/       knob_wigglrs_slew.Value() * max_slew_ms, 
        /*jump_up=*/             sw1_re,
        /*jump_down=*/           sw2_re,
        /*jump_semitones=*/     jump_semitones,
        /*footswitch_rising=*/  fsw1_rising,
        /*footswitch_held=*/    fsw1_held, 
        /*led_wrap=*/           led1_wrap, 
        /*may_skip_trig=*/      may_skip_trig, 
        /*skip_prob=*/          skip_prob
    );

    configure_worm(
        /*wigglr=*/             wigglr2, 
        /*level=*/              knob_wigglr2_vol.Value(),
        /*overdub=*/            knob_wigglr_odb.Value(),
        /*rate_slew_ms=*/       knob_wigglrs_slew.Value() * max_slew_ms, 
        /*jump_up=*/             sw3_re,
        /*jump_down=*/           sw4_re,
        /*jump_semitones=*/     jump_semitones,
        /*footswitch_rising=*/  fsw2_rising,
        /*footswitch_held=*/    fsw2_held, 
        /*led_wrap=*/           led2_wrap, 
        /*may_skip_trig=*/      may_skip_trig, 
        /*skip_prob=*/          skip_prob
    );
}

/*
 * This runs at a fixed rate, to prepare audio samples
 */
void callback(
    AudioHandle::InterleavingInputBuffer  in,
    AudioHandle::InterleavingOutputBuffer out,
    size_t                                size
    )
{
    hw.ProcessAllControls();
    processTerrariumControls();
    led1_wrap.Process();
    led2_wrap.Process();

    for(size_t i = 0; i < size; i += 2)
    {
        wigglr_in[0] = in[i]; // left channel

        wigglr1.ProcessFrame(wigglr_in, wigglr1_out);
        wigglr2.ProcessFrame(wigglr_in, wigglr2_out);

        out[i] = SoftLimit(
            in[i] + 
            wigglr1_out[0] + wigglr2_out[0]
        ); // mix both wigglrs
    }
}

int main(void)
{
    hw.Init();
    sr = hw.AudioSampleRate();
    hw.seed.SetAudioBlockSize(BLOCK_SIZE);
    
    hw.seed.StartLog(false);
    
    
    led1.Init(hw.seed.GetPin(Terrarium::LED_1), false);
    led2.Init(hw.seed.GetPin(Terrarium::LED_2), false);
    led1_wrap.Init(led1, sr);
    led2_wrap.Init(led2, sr);


    // Initialize your knobs here like so:
    // https://electro-smith.github.io/libDaisy/classdaisy_1_1_parameter.html
    knob_wigglr1_vol. Init(hw.knob[Terrarium::KNOB_1], 0.0f, 1.0f, Parameter::EXPONENTIAL);
    knob_wigglrs_jumpamt. Init(hw.knob[Terrarium::KNOB_2], 0.0f, 1.0f, Parameter::LINEAR);
    knob_wigglr2_vol. Init(hw.knob[Terrarium::KNOB_3], 0.0f, 1.0f, Parameter::EXPONENTIAL);
    knob_wigglr_odb.  Init(hw.knob[Terrarium::KNOB_4], 0.0f, 1.0f, Parameter::EXPONENTIAL);
    knob_wigglrs_slew.Init(hw.knob[Terrarium::KNOB_5], 0.0f, 1.0f, Parameter::EXPONENTIAL);
    knob_wigglr_skip. Init(hw.knob[Terrarium::KNOB_6], 0.0f, 1.0f, Parameter::LINEAR);

    wigglr1.Init(sr, wigglr1_buf, WIGGLR_BUF_SIZE, WIGGLR_CHANS);
    wigglr2.Init(sr, wigglr2_buf, WIGGLR_BUF_SIZE, WIGGLR_CHANS);

    skip_metro.Init(1 / 0.1f, sr);

    hw.StartAdc();
    hw.StartAudio(callback);


    while(1)
    {
        // Do lower priority stuff infinitely here
        System::Delay(200);
        // hw.seed.PrintLine("wigglr1 State: %d", (int)wigglr1.GetState());
        // hw.seed.PrintLine("wigglr2 State: %d", (int)wigglr2.GetState());
        // // led1.Set(sw1 ? 0.0f : 1.0f);

        // // // print the window idxs
        // hw.seed.PrintLine("wigglr1 win idx: %d", wigglr1.win_idx_);
        // hw.seed.PrintLine("wigglr2 win idx: %d", wigglr2.win_idx_);

        // // print the pos
        // hw.seed.PrintLine("wigglr1 pos: %d", (int)wigglr1.pos_);
        // hw.seed.PrintLine("wigglr2 pos: %d", (int)wigglr2.pos_);

        // // print led states
        // hw.seed.PrintLine("led1 state: %d", (int)led1_wrap.GetState());
        // hw.seed.PrintLine("led2 state: %d", (int)led2_wrap.GetState());

        // print num blinks
        // hw.seed.PrintLine("led1 num blinks: %d", (int)led1_wrap.num_blinks_);
        // hw.seed.PrintLine("led2 num blinks: %d", (int)led2_wrap.num_blinks_);

        // print state
        // hw.seed.Print("S1\t%d\t", (int)wigglr1.GetState());
        // hw.seed.Print("S2\t%d\n", (int)wigglr2.GetState());

        // // print window idxs
        // hw.seed.Print("W1\t%d\t", (int)wigglr1.win_idx_);
        // hw.seed.Print("W2\t%d\n", (int)wigglr2.win_idx_);

        // // print pos
        // hw.seed.Print("P1\t%d\t", (int)wigglr1.pos_);
        // hw.seed.Print("P2\t%d\n", (int)wigglr2.pos_);

        // hw.seed.PrintLine("");

        // print led states
        // hw.seed.Print("L1\t%d\t", (int)led1_wrap.GetState());
        // hw.seed.Print("L2\t%d\n", (int)led2_wrap.GetState());

        hw.seed.Print("S1\tW1\tP1\tS2\tW2\tP2\n");
        hw.seed.Print("%d\t%d\t%d\t%d\t%d\t%d\n", 
            (int)wigglr1.GetState(), 
            (int)wigglr1.win_idx_, 
            (int)wigglr1.pos_,

            (int)wigglr2.GetState(), 
            (int)wigglr2.win_idx_, 
            (int)wigglr2.pos_
        );
        hw.seed.PrintLine("--------------------------------");
        // print whatever is in the wigglr in and wigglr out buffers
        hw.seed.Print("Win\t%.2f\n", 
            wigglr_in[0]
        );
        hw.seed.Print("W1\t%.2f\t%.2f\n", 
            wigglr1_out[0], wigglr2_out[0]
        );
        hw.seed.PrintLine("--------------------------------");

        // print the sig_ in each wigglr
        hw.seed.Print("Sig1\t");
        for (size_t i = 0; i < wigglr1.sig_.size(); ++i) {
            hw.seed.Print("%.2f ", wigglr1.sig_[i]);
        }
        hw.seed.PrintLine("");
        hw.seed.Print("Sig2\t");
        for (size_t i = 0; i < wigglr2.sig_.size(); ++i) {
            hw.seed.Print("%.2f ", wigglr2.sig_[i]);
        }
        hw.seed.PrintLine("");
        
        // print all of the window values for the wiggler
        // for (size_t i = 0 ; i < wigglr1.kWindowSamps; ++i) {
        //     hw.seed.Print("%.2f ", wigglr1.WindowVal(i * wigglr1.kWindowFactor));
        // }
        hw.seed.PrintLine("");
        hw.seed.PrintLine("--------------------------------");

        // print the num accumulated for each ipoke in the wigglr
        hw.seed.Print("Poke1\t");
        long num_accum = wigglr1.poker_.num_accumulated_;
        hw.seed.Print("%ld ", num_accum);
        hw.seed.PrintLine("");
        hw.seed.Print("Poke2\t");
        num_accum = wigglr2.poker_.num_accumulated_;
        hw.seed.Print("%ld ", num_accum);
        hw.seed.PrintLine("");


        // print the first 20 samples of the wigglr1 buffer
        hw.seed.Print("Wigglr1 Buf:\t");
        for (size_t i = 0; i < 20 && i < WIGGLR_BUF_SIZE; ++i) {
            hw.seed.Print("%.2f ", wigglr1_buf[i]);
        }
        hw.seed.PrintLine("");  
        // print the first 20 samples of the wigglr2 buffer
        hw.seed.Print("Wigglr2 Buf:\t");
        for (size_t i = 0; i < 20 && i < WIGGLR_BUF_SIZE; ++i) {
            hw.seed.Print("%.2f ", wigglr2_buf[i]);
        }
        hw.seed.PrintLine("");


        // log d_start_, d_end_, d_step_ for each ipoke
        hw.seed.Print("Ipoke1:\tStart: %ld\tEnd: %ld\tStep: %ld\n", 
            wigglr1.poker_.d_start_, 
            wigglr1.poker_.d_end_, 
            wigglr1.poker_.d_step_
        );
        hw.seed.Print("Ipoke2:\tStart: %ld\tEnd: %ld\tStep: %ld\n", 
            wigglr2.poker_.d_start_, 
            wigglr2.poker_.d_end_, 
            wigglr2.poker_.d_step_    
        );

        // log max_gaps_filled for each ipoke
        hw.seed.Print("Ipoke1 Max Gaps Filled:\t%ld\n",
            wigglr1.poker_.d_max_gaps_filled_
        );
        hw.seed.Print("Ipoke2 Max Gaps Filled:\t%ld\n",
            wigglr2.poker_.d_max_gaps_filled_
        );
        hw.seed.PrintLine("================================");

    }
}
