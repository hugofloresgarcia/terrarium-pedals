#include "daisy_petal.h"

#include "terrarium.h"
#include "daisysp.h"

#include "lib/cenote_delay.h"
#include "vibrato.h"
#include "xfade.h"
#include "lib/state.h"

#include "lib/cenote_delay.h"
#include "vibrato.h"
#include "xfade.h"

using namespace daisy;
using namespace daisysp;
using namespace terrarium;


// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GLOBALS - HARDWARE
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

constexpr static float kMomentaryFswTimeMs = 300.0f; // Time to hold footswitch for momentary bypass mode
static constexpr float kShiftMaxLarge = 150.0f; // 150 hz if sw3 is pressed
static constexpr float kShiftMaxSmall = 15.0f; // 15 hz if sw3 is not pressed

#define MAX_DELAY_MS_LARGE 1500.0f
#define MAX_DELAY_MS_SMALL 112.5f


// Declare a global daisy_petal for hardware access
DaisyPetal  hw;

Led         led1, led2;
Parameter   knob2, // delay time
            knob3, // feedback
            knob6,  // level
            knob4,  // vibrato depth
            knob1,  // vibrato rate
            knob5;  // shift amount


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

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GLOBALS - DSP
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

CenoteDelayEngine del;
VibratoEngine vibrato; // Two engines for stereo vibrato
Oscillator updown_lfo; // Up/Down LFO for freqshift

daisysp::Line bypass_ramp; // Ramp for bypassing the delay
float ramp_time_ms = 25.0f;

Xfade xfade;

bool prev_bypass_state = false; // Previous bypass state for ramping

// TerrariumControlRecorder control_recorder; // Control recorder for recording knob movements

// pot and switch values
TerrariumState s;
// State for footswitches
FswState fsw1, fsw2; // Footswitch states

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// HARDWARE
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

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
    }

    if (fsw2.pressed && fsw2.time_held > kMomentaryFswTimeMs) {
        fsw2.momentary = true;
    } else if (fsw2.falling && fsw2.momentary) {
        fsw2.momentary = false;
        fsw2.state = false; // disengage bypass mode
    }

}


// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// CONTROL BLOCK
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

/*
 * Process terrarium pots and switches
 */
void controlBlock() {
    // update footswitches
    processFootSwitches(fsw1, fsw2);

    // update knob values
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

    // set knob2 to delay time (sw3 selects time range)
    del.SetDelayMs(
        s.pot2 * (s.sw3 ? MAX_DELAY_MS_LARGE : MAX_DELAY_MS_SMALL)
    );

    // set knob3 to feedback (fsw2 is "infinite" hold)
    del.SetFeedback(
        fsw2.state ? 1.0f : (s.pot3 * 0.9999999999f)
    );

    // set knob5 to pitch shift amount (sign from sw4, range from sw3)
    {
        float up_or_down = s.sw4 ? 1.0f : -1.0f;
        float shift_mult = s.sw3 ? kShiftMaxLarge : kShiftMaxSmall;
        del.SetTransposition(up_or_down * (s.pot5 * shift_mult));
        del.SetBypassFrequencyShift(!s.sw2); // bypass freq shifter if sw2 is pressed
    }

    // vibrato depth/rate (+ disable at tiny depths for latency reasons)
    {
        float lfodepth = s.sw1 ? 1.0f : s.pot4 * 0.5f;
        vibrato.SetLfoDepth(lfodepth);
        vibrato.SetLfoFreq(s.pot1 * 15.0f + 0.1f);
        (s.pot4 < 0.1f) ? vibrato.SetMix(0.0f) : vibrato.SetMix(1.0f);
    }

    led1.Update();
    led2.Update();

    // xfade level is active when either fsw engaged
    xfade.SetCrossfade(
        (fsw1.state || fsw2.state) ? knob6.Value() : 0.0f
    );
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// AUDIO BLOCK
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

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
    controlBlock();


    float del_out;
    float sig;
    float delay_in; 

    bool new_bypass_state = (fsw1 || fsw2);
    if (new_bypass_state != prev_bypass_state) {
        // Bypass state changed, ramp the bypass
        bypass_ramp.Start(
            (uint8_t)prev_bypass_state, 
            (uint8_t)new_bypass_state, 
            ramp_time_ms * 0.001f
        );
        prev_bypass_state = new_bypass_state;
    }

    uint8_t ramp_finished = 0;
    for(size_t i = 0; i < size; i += 2)
    {
        sig = in[i];

        // vibrato is always on
        sig = vibrato.Process(sig);
        
        // process delay
        delay_in = sig * bypass_ramp.Process(&ramp_finished); // ramp the input to the delay
        del_out = del.Process(
            delay_in, 
            /*clip=*/true, 
            /*limit=*/fsw2
        );

        // mix delay
        sig = xfade.Process(sig, del_out * 1.414f); // little oomph

        // softclip out
        out[i] = SoftClip(sig); // Soft clipping
        // out[i] = del_out; // Soft clipping
    }
}


void init() {
    float sr;

    hw.Init();
    sr = hw.AudioSampleRate();

    // keep your block-size choice (harmless on sandbox)
    hw.SetAudioBlockSize(2);
    hw.seed.StartLog(false);

    led1.Init(hw.seed.GetPin(Terrarium::LED_1), false);
    led2.Init(hw.seed.GetPin(Terrarium::LED_2), false);

    // knobs
    knob2.Init(hw.knob[Terrarium::KNOB_2], 0.0f, 1.0f, Parameter::CUBE);
    knob3.Init(hw.knob[Terrarium::KNOB_3], 0.0f, 1.0f, Parameter::LINEAR);
    knob6.Init(hw.knob[Terrarium::KNOB_6], 0.0f, 1.0f, Parameter::LINEAR);
    knob1.Init(hw.knob[Terrarium::KNOB_1], 0.0f, 1.0f, Parameter::LINEAR);
    knob4.Init(hw.knob[Terrarium::KNOB_4], 0.0f, 1.0f, Parameter::LINEAR);
    knob5.Init(hw.knob[Terrarium::KNOB_5], 0.0f, 1.0f, Parameter::EXPONENTIAL);

    // engines
    del.Init(sr);
    vibrato.Init(sr);

    updown_lfo.Init(sr);
    updown_lfo.SetWaveform(Oscillator::WAVE_SQUARE);
    updown_lfo.SetFreq(0.0f); 
    updown_lfo.SetAmp(1.0f);  
    updown_lfo.Reset(0.0f); // Reset to 0 phase

    bypass_ramp.Init(sr);
    bypass_ramp.Start(0.0f, 0.0f, ramp_time_ms * 0.001f);

    // control_recorder.Init(); // (no-op in sandbox unless you wire it)

    xfade.Init(sr, 10.0f);
    xfade.SetCrossfadeType(Xfade::TYPE::ASYMMETRIC_MIX); // power crossfade
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ENTRY POINT / SANDBOX WRAPPER
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#if defined(BUILDING_FOR_EMULATOR)
#else  // ====== HARDWARE BUILD ======
int main(void)
{
    init();

    hw.StartAdc();
    hw.StartAudio(callback);

    while(1)
    {
        System::Delay(10);
    }
}
#endif // BUILDING_FOR_EMULATOR
