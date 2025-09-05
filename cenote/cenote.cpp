#include "daisy_petal.h"
#include "daisysp.h"
#include "terrarium.h"

#include "lib/cenote_delay.h"
#include "vibrato.h"
#include "lib/control_recorder.h"
// #include "lib/settings.h"

using namespace daisy;
using namespace daisysp;
using namespace terrarium;

constexpr static float kMomentaryFswTimeMs = 300.0f; // Time to hold footswitch for momentary bypass mode
static constexpr float kShiftMaxLarge = 150.0f; // 150 hz if sw3 is pressed
static constexpr float kShiftMaxSmall = 15.0f; // 15 hz if sw3 is not pressed

struct FswState {
    bool state = false; // the "virtual" state of the footswitch
    bool momentary = false; // virtual momentary state for temporary bypass mode
    bool pressed = false; // true if the footswitch is currently pressed
    bool rising = false; // true if the footswitch was just pressed
    bool falling = false; // true if the footswitch was just released
    float time_held = 0.0f; // time the footswitch has been held (ms)

    inline bool operator== (const FswState& other) const {
        return state == other.state;
    }
    inline bool operator!= (const FswState& other) const {
        return !(state == other.state);
    }
    inline bool operator|| (const FswState& other) const {
        return state || other.state;
    }
    inline operator bool() const {
        return state;
    }

};

// GLOBALS

// Declare a global daisy_petal for hardware access
DaisyPetal  hw;

Led         led1, led2;
Parameter   knob2, // delay time
            knob3, // feedback
            knob6,  // level
            knob4,  // vibrato depth
            knob1,  // vibrato rate
            knob5;  // shift amount

CenoteDelayEngine del;
VibratoEngine vibrato; // Two engines for stereo vibrato
Oscillator updown_lfo; // Up/Down LFO for freqshift

Line bypass_ramp; // Ramp for bypassing the delay
float ramp_time_ms = 25.0f;

bool prev_bypass_state = false; // Previous bypass state for ramping

TerrariumControlRecorder control_recorder; // Control recorder for recording knob movements

// pot and switch values
TerrariumState s;
// State for footswitches
FswState fsw1, fsw2; // Footswitch states


void processFootSwitches(FswState &fsw1, FswState &fsw2) {

    fsw1.pressed = hw.switches[Terrarium::FOOTSWITCH_1].Pressed();
    fsw2.pressed = hw.switches[Terrarium::FOOTSWITCH_2].Pressed();

    fsw1.rising = hw.switches[Terrarium::FOOTSWITCH_1].RisingEdge();
    fsw2.rising = hw.switches[Terrarium::FOOTSWITCH_2].RisingEdge();

    fsw1.falling = hw.switches[Terrarium::FOOTSWITCH_1].FallingEdge();
    fsw2.falling = hw.switches[Terrarium::FOOTSWITCH_2].FallingEdge();

    fsw1.time_held = hw.switches[Terrarium::FOOTSWITCH_1].TimeHeldMs();
    fsw2.time_held = hw.switches[Terrarium::FOOTSWITCH_2].TimeHeldMs();

    // update footswitch state
    if(fsw1.rising) {
        fsw1.state = !fsw1.state;
        if (fsw1.state) {fsw2.state = false;} // if fsw1 is pressed, disengage fsw2
    }
    
    if(fsw2.rising) {
        fsw2.state = !fsw2.state;
        if (fsw2.state) {fsw1.state = false;} // if fsw2 is pressed, disengage fsw1
    }

    if (fsw1.pressed && fsw1.time_held > kMomentaryFswTimeMs) {
        fsw1.momentary = true;
    } else if (fsw1.falling && fsw1.momentary) {
        fsw1.momentary = false;
        // the line below is needed if we're nuking the control recorder code. 
        fsw1.state = false; // disengage bypass mode
        
        // only disengage bypass mode if control recorder is idle (DISABLED)
        // if (control_recorder.GetState() == TerrariumControlRecorder::CtrlRecorderState::IDLE) {
        //     fsw1.state = false; 
        // }
    }

    // TODO: fsw2 is held pressed for longer than 300ms, it is in temporary bypass mode
    // and should be disengaged after it is released
    if (fsw2.pressed && fsw2.time_held > kMomentaryFswTimeMs) {
        fsw2.momentary = true;
    } else if (fsw2.falling && fsw2.momentary) {
        fsw2.momentary = false;
        fsw2.state = false; // disengage bypass mode
    }

    // CONTROL RECORDER (not ready yet)
    // if both footswitches are in momentary mode, start recording
    // if (fsw1.momentary && fsw2.momentary) {
    //     if (control_recorder.GetState() == 
    //         TerrariumControlRecorder::CtrlRecorderState::IDLE) {
    //         control_recorder.StartRecording();
    //         control_recorder.SetListenForOverrides(false);
    //     }
    // }
    // else if (control_recorder.GetState() == 
    //         TerrariumControlRecorder::CtrlRecorderState::RECORDING 
    //         && (fsw1.falling || fsw2.falling)) {
    //     // if both footswitches are released, stop recording and start playing
    //     if (control_recorder.GetState() == 
    //         TerrariumControlRecorder::CtrlRecorderState::RECORDING) {
    //         control_recorder.StartPlaying();
    //         control_recorder.SetListenForOverrides(true);
    //     }
    // }
    // if ((fsw1.rising || fsw2.rising) && control_recorder.GetState() == 
    //         TerrariumControlRecorder::CtrlRecorderState::PLAYING) {
    //     control_recorder.StopPlaying();
    //     fsw1.state = false; 
    //     fsw2.state = false; 
    // }
}


/*
 * Process terrarium pots and switches
 */
void processTerrariumControls() {
    // update footswitches
    processFootSwitches(fsw1, fsw2);

    // update knob values
    // https://electro-smith.github.io/libDaisy/classdaisy_1_1_analog_control.html
    knob1.Process();
    knob2.Process();
    knob3.Process();
    knob4.Process();
    knob5.Process();
    knob6.Process();

    // update state
    s.pot1 = knob1.Value();
    s.pot2 = knob2.Value();
    s.pot3 = knob3.Value();
    s.pot4 = knob4.Value();
    s.pot5 = knob5.Value();
    s.pot6 = knob6.Value();

    s.sw1 = hw.switches[Terrarium::SWITCH_1].Pressed();
    s.sw2 = hw.switches[Terrarium::SWITCH_2].Pressed();
    s.sw3 = hw.switches[Terrarium::SWITCH_3].Pressed();
    s.sw4 = hw.switches[Terrarium::SWITCH_4].Pressed();
    
    // update LEDS
    led1.Set(fsw1.state ? 1.0f : 0.0f);
    led2.Set(fsw2.state ? 1.0f : 0.0f);

    // // process control recorder 
    // NOTE: control recorder is not ready yet. 
    // control_recorder.Process(s);

    // set knob2 to delay time
    float delay_time_mult = s.sw3 ? 1.0f : 0.075f; // 0.5x if sw3 is pressed, otherwise 1.0x
    del.SetDelayMs(s.pot2 * del.GetMaxDelayMs() * delay_time_mult); //

    // set knob3 to feedback
    del.SetFeedback(
        fsw2.state ? 1.0f : // fsw2 is full feedback mode.
            s.pot3 * 0.9999999999f
    );

    // set knob_shiftamt to pitch shift amount
    float up_or_down = s.sw4 ? 1.0f : -1.0f; // negative if sw4 is pressed
    float shift_mult = s.sw3 ? kShiftMaxLarge : kShiftMaxSmall;
    del.SetTransposition(up_or_down * (s.pot5 * shift_mult));
    del.SetBypassFrequencyShift(!s.sw2); // bypass frequency shifting if sw2 is pressed

    // set knob_vibdepth to vibrato depth
    // if sw1 is pressed, set it to "extreme" depth
    // otherwise, set it to a fraction of the knob value
    float lfodepth = s.sw1 ? 1.0f : s.pot4 * 0.5f; // 1.0 if sw1 is pressed, otherwise 0.25x knob value
    vibrato.SetLfoDepth(lfodepth);
    vibrato.SetLfoFreq(s.pot1 * 15.0f + 0.1f);

    // disable vibrato at too low depth to avoid latency
    s.pot4 < 0.1f ? vibrato.SetMix(0.0f) : vibrato.SetMix(1.0f);

    led1.Update();
    led2.Update();

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

        del_out = knob6.Value() * del_out; // apply level control
        
        // mix delay
        sig = sig + del_out * 1.5f; // little oomph
        
        // softclip out
        out[i] = SoftClip(sig); // Soft clipping

    }
}

int main(void)
{
    float sr;

    hw.Init();
    sr = hw.AudioSampleRate();

    // CRITICAL: block size must be 2 to avoid 16kHz noise introduced by the  terrarium circuit or the seed itself!
    // according to the interwebs, the noise drops an octave every time the block size is doubled.
    // 2 is out of the nyquist range, so its filtered out. 
    // see https://github.com/bkshepherd/DaisySeedProjects/issues/9
    hw.SetAudioBlockSize(2); 
    hw.seed.StartLog(false);

    led1.Init(hw.seed.GetPin(Terrarium::LED_1), false);
    led2.Init(hw.seed.GetPin(Terrarium::LED_2), false);

    // Initialize your knobs here like so:
    // https://electro-smith.github.io/libDaisy/classdaisy_1_1_parameter.html
    knob2.Init(hw.knob[Terrarium::KNOB_2], 0.0f, 1.0f, Parameter::EXPONENTIAL);
    knob3.Init(hw.knob[Terrarium::KNOB_3], 0.0f, 1.0f, Parameter::EXPONENTIAL);
    knob6.Init(hw.knob[Terrarium::KNOB_6], 0.0f, 1.0f, Parameter::EXPONENTIAL);
    knob1.Init(hw.knob[Terrarium::KNOB_1], 0.0f, 1.0f, Parameter::LINEAR);
    knob4.Init(hw.knob[Terrarium::KNOB_4], 0.0f, 1.0f, Parameter::LINEAR);
    knob5.Init(hw.knob[Terrarium::KNOB_5], 0.0f, 1.0f, Parameter::LINEAR);

    // Set sr for your processing like so:
    del.Init(sr);
    vibrato.Init(sr);

    updown_lfo.Init(sr);
    updown_lfo.SetWaveform(Oscillator::WAVE_SQUARE);
    updown_lfo.SetFreq(0.0f); 
    updown_lfo.SetAmp(1.0f);  
    updown_lfo.Reset(0.0f); // Reset to 0 phase

    bypass_ramp.Init(sr);

    control_recorder.Init(); // Initialize control recorder

    hw.StartAdc();
    hw.StartAudio(callback);

    while(1)
    {
        // Do lower priority stuff infinitely here
        // hw.seed.PrintLine("Vibrato LFO Depth: %d ", int(knob4.Value() * 1000));
        System::Delay(10);
    }
}
