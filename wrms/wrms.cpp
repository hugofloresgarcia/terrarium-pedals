#include "daisy_petal.h"
#include "daisysp.h"
#include "terrarium.h"
#include "lib/wrm.h"

using namespace daisy;
using namespace daisysp;
using namespace terrarium;

// Declare a global daisy_petal for hardware access
DaisyPetal  hw;

bool        fsw1, fsw2, sw1, sw2, sw3, sw4;
bool        fsw1_momentary, fsw2_momentary = false; // for temporary bypass mode
Led         led1, led2;

Parameter   knob1_wrm1_vol, 
            knob2_wrms_slew, 
            knob3_wrm2_vol, 
            knob4_wrm1_odb, 
            knob5_wrms_, 
            knob6_wrm2_odb;

float sr;

constexpr static size_t wrm_buf_size = 48000 * 10; // 10 seconds of audio at 48kHz
float DSY_SDRAM_BSS wrm1_buf[wrm_buf_size]; // 10 seconds of audio at 48kHz
float DSY_SDRAM_BSS wrm2_buf[wrm_buf_size];

// constexpr static size_t wrm_buf_size = 48000; // 10 seconds of audio at 48kHz
// float wrm1_buf[wrm_buf_size];
// float wrm2_buf[wrm_buf_size];

WrmsLooper wrm1, wrm2;

float fsw_held_ms = 300.f;
float max_slew_ms = 3000.f;


void configure_worm(WrmsLooper &wrm, float level, float overdub, 
    float rate_slew_ms, bool oct_up, bool oct_down, 
    bool footswitch_rising, bool footswitch_held)
{
    // WRMS CONFIG
    wrm.SetLevel(level);
    wrm.SetOverdub(overdub);
    wrm.SetRateSlewMs(rate_slew_ms); // convert to milliseconds

    if (oct_down) {
        wrm.SetRateSemitones(
            wrm.GetRateSemitones() - 12.f
        ); // decrease by 1 octave
    }
    if (oct_up) {
        wrm.SetRateSemitones(
            wrm.GetRateSemitones() + 12.f
        ); // increase by 1 octave
    }

    if (footswitch_rising) {
        if (wrm.GetState() == WrmsLooper::State::IDLE)
        { 
            wrm.SetPhase(0.f); // reset position to the start of the loop
            wrm.SetState(WrmsLooper::State::RECORD); 
        }
        else if (wrm.GetState() == WrmsLooper::State::RECORD)
        { 
            wrm.SetLoopPoints(0.0f, wrm.GetPosition()); // set the end point of the loop
            wrm.SetState(WrmsLooper::State::PLAYBACK); 
        }
        else if (wrm.GetState() == WrmsLooper::State::PLAYBACK)
        { wrm.SetState(WrmsLooper::State::OVERDUB); }
        else if (wrm.GetState() == WrmsLooper::State::OVERDUB)
        { wrm.SetState(WrmsLooper::State::PLAYBACK); }
    }

    if (footswitch_held)
    {
        wrm.Reset();
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
    knob1_wrm1_vol.Process();
    knob2_wrms_slew.Process();
    knob3_wrm2_vol.Process();
    knob4_wrm1_odb.Process();
    knob5_wrms_.Process();
    knob6_wrm2_odb.Process();

    // float knob1_wrm1_vol_value = knob1_wrm1_vol.Value();
    // wrm1.SetLevel(knob1_wrm1_vol.Value());
    // wrm1.SetLevel(0.f);

    configure_worm(
        /*wrm=*/wrm1, 
        /*level=*/knob1_wrm1_vol.Value(),
        /*overdub=*/knob4_wrm1_odb.Value(),
        /*rate_slew_ms=*/knob2_wrms_slew.Value() * max_slew_ms, 
        /*oct_up=*/sw1_re,
        /*oct_down=*/sw2_re,
        /*footswitch_rising=*/fsw1_rising,
        /*footswitch_held=*/fsw1_held
    );

    configure_worm(
        /*wrm=*/wrm2, 
        /*level=*/knob3_wrm2_vol.Value(),
        /*overdub=*/knob6_wrm2_odb.Value(),
        /*rate_slew_ms=*/knob2_wrms_slew.Value() * max_slew_ms, 
        /*oct_up=*/sw3_re,
        /*oct_down=*/sw4_re,
        /*footswitch_rising=*/fsw2_rising,
        /*footswitch_held=*/fsw2_held
    );

    led1.Set(wrm1.GetState() == WrmsLooper::State::IDLE ? 0.0f : 1.0f);
    led2.Set(wrm2.GetState() == WrmsLooper::State::IDLE ? 0.0f : 1.0f);


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
    led1.Update();
    led2.Update();

    float wrm1_out = 0.f;
    float wrm2_out = 0.f;

    for(size_t i = 0; i < size; i += 2)
    {
        wrm1_out = wrm1.Process(in[i]);
        wrm2_out = wrm2.Process(in[i]);

        out[i] = in[i] + wrm1_out + wrm2_out; // mix both wrms
    }
}

int main(void)
{
    hw.Init();
    sr = hw.AudioSampleRate();
    // hw.seed.SetAudioBlockSize(1024);

    hw.seed.StartLog(false);

    led1.Init(hw.seed.GetPin(Terrarium::LED_1), false);
    led2.Init(hw.seed.GetPin(Terrarium::LED_2), false);

    // Initialize your knobs here like so:
    // https://electro-smith.github.io/libDaisy/classdaisy_1_1_parameter.html
    knob1_wrm1_vol.Init(hw.knob[Terrarium::KNOB_1], 0.0f, 1.0f, Parameter::EXPONENTIAL);
    knob2_wrms_slew.Init(hw.knob[Terrarium::KNOB_2], 0.0f, 1.0f, Parameter::EXPONENTIAL);
    knob3_wrm2_vol.Init(hw.knob[Terrarium::KNOB_3], 0.0f, 1.0f, Parameter::EXPONENTIAL);
    knob4_wrm1_odb.Init(hw.knob[Terrarium::KNOB_4], 0.0f, 1.0f, Parameter::EXPONENTIAL);
    knob5_wrms_.Init(hw.knob[Terrarium::KNOB_5], 0.0f, 1.0f, Parameter::EXPONENTIAL);
    knob6_wrm2_odb.Init(hw.knob[Terrarium::KNOB_6], 0.0f, 1.0f, Parameter::EXPONENTIAL);

    wrm1.Init(sr, wrm1_buf, wrm_buf_size);
    wrm2.Init(sr, wrm2_buf, wrm_buf_size);

    hw.StartAdc();
    hw.StartAudio(callback);


    while(1)
    {
        // Do lower priority stuff infinitely here
        System::Delay(50);
        hw.seed.PrintLine("WRM1 State: %d", (int)wrm1.GetState());
        hw.seed.PrintLine("WRM2 State: %d", (int)wrm2.GetState());
        // led1.Set(sw1 ? 0.0f : 1.0f);

        // hw.seed.PrintLine("WRM1 State: %d", (int)wrm1.GetsState());
        // led2.Set(wrm2.GetState() == WrmsLooper::State::IDLE ? 0.0f : 1.0f);

        // print pos and rate (cast to int first)
        hw.seed.PrintLine("WRM1 Pos: %d, Rate: %d",
            (int)(wrm1.GetPosition() * wrm_buf_size), 
            (int)wrm1.GetRateSemitones()
        );

        hw.seed.PrintLine("WRM2 Pos: %d, Rate: %d",
            (int)(wrm2.GetPosition() * wrm_buf_size), 
            (int)wrm2.GetRateSemitones()
        );
    }
}
