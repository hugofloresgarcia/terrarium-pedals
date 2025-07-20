#include "daisy_petal.h"
#include "daisysp.h"
#include "terrarium.h"

#include "lib/cenote_delay.h"
#include "lib/vibrato.h"
#include "lib/control_recorder.h"

using namespace daisy;
using namespace daisysp;
using namespace terrarium;

// Declare a global daisy_petal for hardware access
DaisyPetal  hw;

bool        fsw1, fsw2, sw1, sw2, sw3, sw4;
bool        fsw1_momentary, fsw2_momentary = false; // for temporary bypass mode
Led         led1, led2;
Parameter   knob_delaytime, 
            knob_delayfb, 
            knob_level, 
            knob_vibdepth, 
            knob_vibrate, 
            knob_shiftamt;

CenoteDelayEngine del;
VibratoEngine vibrato; // Two engines for stereo vibrato
Oscillator updown_lfo; // Up/Down LFO for freqshit 

Line bypass_ramp; // Ramp for bypassing the delay
float ramp_time_ms = 25.0f;

bool prev_bypass_state = false; // Previous bypass state for ramping

TerrariumControlRecorder control_recorder; // Control recorder for recording knob movements

// parameter values (for overriding with control recorder)
float knob_delaytime_val, 
      knob_delayfb_val, 
      knob_level_val, 
      knob_vibdepth_val, 
      knob_vibrate_val, 
      knob_shiftamt_val;
/*
 * Process terrarium knobs and switches
 */
void processTerrariumControls() {
    // update footswitches
    bool fsw1_re = hw.switches[Terrarium::FOOTSWITCH_1].RisingEdge();
    bool fsw2_re = hw.switches[Terrarium::FOOTSWITCH_2].RisingEdge();

    bool fsw1_pressed = hw.switches[Terrarium::FOOTSWITCH_1].Pressed();
    bool fsw2_pressed = hw.switches[Terrarium::FOOTSWITCH_2].Pressed();

    bool fsw1_falling = hw.switches[Terrarium::FOOTSWITCH_1].FallingEdge();
    bool fsw2_falling = hw.switches[Terrarium::FOOTSWITCH_2].FallingEdge();

    bool fsw1_rising = hw.switches[Terrarium::FOOTSWITCH_1].RisingEdge();
    bool fsw2_rising = hw.switches[Terrarium::FOOTSWITCH_2].RisingEdge();

    float fsw1_time_held = hw.switches[Terrarium::FOOTSWITCH_1].TimeHeldMs();
    float fsw2_time_held = hw.switches[Terrarium::FOOTSWITCH_2].TimeHeldMs();

    // toggle footswitches
    if(fsw1_re) {
        fsw1 = !fsw1;
    }
    
    if(fsw2_re) {
        fsw2 = !fsw2;
    }

    if ((fsw1_re || fsw2_re) && control_recorder.GetState() == 
            TerrariumControlRecorder::CtrlRecorderState::PLAYING) {
        control_recorder.StopPlaying();
        fsw1 = false; 
        fsw2 = false; 
    }

    if (fsw1_pressed && fsw1_time_held > 150.0f) {
        fsw1_momentary = true;
    } else if (fsw1_falling && fsw1_momentary) {
        fsw1_momentary = false;

        // only disengage bypass mode if control recorder is idle
        if (control_recorder.GetState() == TerrariumControlRecorder::CtrlRecorderState::IDLE) {
            fsw1 = false; 
        }
    }

    // TODO: fsw2 is held pressed for longer than 300ms, it is in temporary bypass mode
    // and should be disengaged after it is released
    if (fsw2_pressed && fsw2_time_held > 150.0f) {
        fsw2_momentary = true;
    } else if (fsw2_falling && fsw2_momentary) {
        fsw2_momentary = false;
        fsw2 = false; // disengage bypass mode
    }

    // if both footswitches are in momentary mode, start recording
    if (fsw1_momentary && fsw2_momentary) {
        if (control_recorder.GetState() == 
            TerrariumControlRecorder::CtrlRecorderState::IDLE) {
            control_recorder.StartRecording();
            control_recorder.SetListenForOverrides(false);
        }
    }
    else if (fsw1_falling || fsw2_falling) {
        // if both footswitches are released, stop recording
        if (control_recorder.GetState() == 
            TerrariumControlRecorder::CtrlRecorderState::RECORDING) {
            control_recorder.StartPlaying();
            control_recorder.SetListenForOverrides(true);
        }
    }

    // update knob values
    // https://electro-smith.github.io/libDaisy/classdaisy_1_1_analog_control.html
    knob_delaytime.Process();
    knob_delayfb.Process();
    knob_level.Process();
    knob_vibdepth.Process();
    knob_vibrate.Process();
    knob_shiftamt.Process();

    led1.Set(fsw1 ? 1.0f : 0.0f);
    led2.Set(fsw2 ? 1.0f : 0.0f);
    
    if (control_recorder.GetState() == TerrariumControlRecorder::CtrlRecorderState::PLAYING &&
        control_recorder.GetIndex() < 32) {
        led1.Set(0.0f); // blink off to indicate cycle restart
    }
    // if (control_recorder.GetState() == TerrariumControlRecorder::CtrlRecorderState::PLAYING) {
    //     // alternate on and off every 64 indices 
    //     uint32_t blink_index_div = control_recorder.GetIndex() / 64;
    //     if (blink_index_div % 2 == 0) {
    //         led2.Set(1.0f); // blink on
    //     } else {
    //         led2.Set(0.0f); // blink off
    //     }
    // }

    sw1 = hw.switches[Terrarium::SWITCH_1].Pressed();
    sw2 = hw.switches[Terrarium::SWITCH_2].Pressed();
    sw3 = hw.switches[Terrarium::SWITCH_3].Pressed();
    sw4 = hw.switches[Terrarium::SWITCH_4].Pressed();

    // get the values for the knobs and switches
    knob_delaytime_val = knob_delaytime.Value();
    knob_delayfb_val = knob_delayfb.Value();
    knob_level_val = knob_level.Value();
    knob_vibdepth_val = knob_vibdepth.Value();
    knob_vibrate_val = knob_vibrate.Value();
    knob_shiftamt_val = knob_shiftamt.Value();

    // CONTROL RECORDER STUFF
    float pots[6] = {
        knob_delaytime_val,
        knob_delayfb_val,
        knob_level_val,
        knob_vibdepth_val,
        knob_vibrate_val,
        knob_shiftamt_val
    };
    bool switches[4] = {sw1, sw2, sw3, sw4};

    control_recorder.Process(pots, switches);

    // Update the knobs and switches with the recorded values
    knob_delaytime_val = pots[0];
    knob_delayfb_val = pots[1];
    knob_level_val = pots[2];
    knob_vibdepth_val = pots[3];
    knob_vibrate_val = pots[4];
    knob_shiftamt_val = pots[5];
    sw1 = switches[0];
    sw2 = switches[1];
    sw3 = switches[2];
    sw4 = switches[3];

    // CONFIGURE THE ENGINES

    // set knob_delaytime to delay time
    float delay_time_mult = sw3 ? 1.0f : 0.075f; // 0.5x if sw3 is pressed, otherwise 1.0x
    del.SetDelayMs(knob_delaytime_val * del.GetMaxDelayMs() * delay_time_mult); //

    // set knob_delayfb to feedback
    del.SetFeedback(
        fsw2 ? 1.0f : // fsw2 is full feedback mode.
            knob_delayfb_val * 0.9999999999f

    );

    // set knob_shiftamt to pitch shift amount
    float up_or_down = sw4 ? 1.0f : -1.0f; // negative if sw4 is pressed

    float shift_mult = sw3 ? 150.0f : 15.0f; // 150 hz if sw3 is pressed, otherwise 15 hz
    del.SetTransposition(up_or_down * (knob_shiftamt_val * shift_mult));
    del.SetBypassFrequencyShift(!sw2); // bypass frequency shifting if sw2 is pressed

    // set knob_vibdepth to vibrato depth
    // if sw1 is pressed, set it to "extreme" depth
    // otherwise, set it to a fraction of the knob value
    vibrato.SetLfoDepth(
        sw1 ? 0.9f : knob_vibdepth_val * 0.25f
    );

    // set knob_vibrate to vibrato frequency
    vibrato.SetLfoFreq(knob_vibrate_val * 15.0f);

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

    float del_out;
    float sig;
    float delay_in; 

    bool new_bypass_state = (fsw1 || fsw2);
    if (new_bypass_state != prev_bypass_state) {
        // Bypass state changed, ramp the bypass
        if (new_bypass_state) {
            bypass_ramp.Start(0.0f, 1.0f, ramp_time_ms * 0.001f); // ramp up to 1.0
        } else {
            bypass_ramp.Start(1.0f, 0.0f, ramp_time_ms * 0.001f); // ramp down to 0.0
        }
        prev_bypass_state = new_bypass_state;
    }

    for(size_t i = 0; i < size; i += 2)
    {
        sig = in[i];

        // vibrato is always on
        sig = vibrato.Process(sig);
    
        if (new_bypass_state) {
            delay_in = sig;
        } else {
            delay_in = 0.0f; 
        }
        
        // process delay
        del_out = del.Process(delay_in, /*clip=*/true, /*limit=*/fsw2);

        del_out = knob_level.Value() * del_out; // apply level control
        
        // mix delay
        out[i] = sig + del_out;
        
        // softclip out
        out[i] = SoftClip(out[i]); // Soft clipping

    }
}

int main(void)
{
    float sr;

    hw.Init();
    sr = hw.AudioSampleRate();

    led1.Init(hw.seed.GetPin(Terrarium::LED_1), false);
    led2.Init(hw.seed.GetPin(Terrarium::LED_2), false);

    // Initialize your knobs here like so:
    // https://electro-smith.github.io/libDaisy/classdaisy_1_1_parameter.html
    knob_delaytime.Init(hw.knob[Terrarium::KNOB_2], 0.0f, 1.0f, Parameter::EXPONENTIAL);
    knob_delayfb.Init(hw.knob[Terrarium::KNOB_3], 0.0f, 1.0f, Parameter::EXPONENTIAL);
    knob_level.Init(hw.knob[Terrarium::KNOB_6], 0.0f, 1.0f, Parameter::EXPONENTIAL);
    knob_vibrate.Init(hw.knob[Terrarium::KNOB_1], 0.0f, 1.0f, Parameter::LINEAR);
    knob_vibdepth.Init(hw.knob[Terrarium::KNOB_4], 0.0f, 1.0f, Parameter::EXPONENTIAL);
    knob_shiftamt.Init(hw.knob[Terrarium::KNOB_5], 0.0f, 1.0f, Parameter::LINEAR);

    // Set sr for your processing like so:
    // verb.Init(sr);
    del.Init(sr);
    vibrato.Init(sr);

    updown_lfo.Init(sr);
    updown_lfo.SetWaveform(Oscillator::WAVE_SQUARE);
    updown_lfo.SetFreq(0.0f); 
    updown_lfo.SetAmp(1.0f);  
    updown_lfo.Reset(0.0f); // Reset to 0 phase

    bypass_ramp.Init(sr);

    control_recorder.Init(); // Initialize control recorder

    // bypass = false;

    hw.StartAdc();
    hw.StartAudio(callback);

    while(1)
    {
        // Do lower priority stuff infinitely here
        System::Delay(10);
    }
}
