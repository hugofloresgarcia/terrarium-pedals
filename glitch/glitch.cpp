#include "daisy_petal.h"
#include "daisysp.h"
#include "terrarium.h"

#include "lib/glitch.h"
#include "lib/ledwrap.h"
#include "lib/shiftknobman.h"

using namespace daisy;
using namespace daisysp;
using namespace terrarium;

// Declare a global daisy_petal for hardware access
DaisyPetal  hw;

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
constexpr static float kMomentaryFswTimeMs = 300.0f; // Time to hold footswitch for momentary bypass mode


FswState   fsw1, fsw2; // footswitch states

bool        sw1, sw2, sw3, sw4;

Led         led1, led2;
LedWrap     ledw1, ledw2;

Parameter   knob_glitch_dur, 
            knob_glitch_spread, 
            knob_pitch, 
            knob_rskip, 
            knob_level, 
            knob_env;

enum KNOB {
    KNOB_GLITCH_DUR = 0,
    KNOB_GLITCH_SPREAD,
    KNOB_PITCH,
    KNOB_RSKIP,
    KNOB_LEVEL,
    KNOB_ENV,
    KNOB_LAST
};

float sr;


#define BUF_SIZE (48000 * 20)  // 60 seconds of audio at 48kHz
#define CHANS 1 // mono :(
#define BLOCK_SIZE 2 // 2 samples per block for audio processing

float DSY_SDRAM_BSS buf[BUF_SIZE * CHANS];

GlitchEngine glitch;

inline float linlin(float x, float a, float b, float c, float d) {
if (x <= a)
    return c;
if (x >= b)
    return d;
return (x - a) / (b - a) * (d - c) + c;
}

ShiftKnobManager skm;

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
        fsw1.state = false; // disengage bypass mode
    }

    if (fsw2.pressed && fsw2.time_held > kMomentaryFswTimeMs) {
        fsw2.momentary = true;
    } else if (fsw2.falling && fsw2.momentary) {
        fsw2.momentary = false;
        fsw2.state = false; // disengage bypass mode
    }
}


/*
 * Process terrarium knobs and switches
 */
void processTerrariumControls() {
    // update switch values
    // https://electro-smith.github.io/libDaisy/classdaisy_1_1_switch.html

    // process footswitches
    processFootSwitches(fsw1, fsw2);

    sw1 = hw.switches[Terrarium::SWITCH_1].Pressed();
    sw2 = hw.switches[Terrarium::SWITCH_2].Pressed();
    sw3 = hw.switches[Terrarium::SWITCH_3].Pressed();
    sw4 = hw.switches[Terrarium::SWITCH_4].Pressed();

    // update knob values
    // https://electro-smith.github.io/libDaisy/classdaisy_1_1_analog_control.html
    knob_glitch_dur.Process();
    knob_glitch_spread.Process();
    knob_pitch.Process();
    knob_rskip.Process();
    knob_level.Process();
    knob_env.Process();

}

void controlBlock() {
    // process the shift knob manager
    std::array<float, 8> hw_knobs;
    hw_knobs[0] = knob_glitch_dur.Value();
    hw_knobs[1] = knob_glitch_spread.Value();
    hw_knobs[2] = knob_pitch.Value();
    hw_knobs[3] = knob_rskip.Value();
    hw_knobs[4] = knob_level.Value();
    hw_knobs[5] = knob_env.Value();
    hw_knobs[6] = 0.0f; // unused
    hw_knobs[7] = 0.0f; // unused
    
    skm.SetShift(sw1);
    bool takeover = skm.ProcessKnobs(hw_knobs);
    

    float glitch_dur = linlin(
        skm.GetNormalValue(KNOB_GLITCH_DUR), 
        0.0f, 1.0f, 20.0f, 2000.0f
    );
    // add a random amount to the glitch duration of up to 50% of the duration
    float rand_dur_amt = skm.GetShiftValue(KNOB_GLITCH_DUR);
    float rand_dur = rand_dur_amt * randf(-0.5f * glitch_dur, 0.5f * glitch_dur);
    glitch_dur += rand_dur;
    glitch_dur = fclamp(glitch_dur, 20.f, 5000.f); // clamp between 20ms and 5000ms

    float glitch_spread = skm.GetNormalValue(KNOB_GLITCH_SPREAD);

    float pitch = linlin(
        skm.GetNormalValue(KNOB_PITCH), 
        0.0f, 1.0f, -12.0f, 12.0f
    );
    // round pitch to the nearest semitone
    pitch = roundf(pitch);
    float pitch_spread = skm.GetShiftValue(KNOB_PITCH) * 24.0f; // up to +/- 12 semitones of pitch spread
    pitch_spread = fclamp(pitch_spread, 0.f, 12.f); // clamp between 0 and 12 semitones


    float rskip = skm.GetNormalValue(KNOB_RSKIP);

    float level = skm.GetNormalValue(KNOB_LEVEL);

    float env_atk_amt = skm.GetNormalValue(KNOB_ENV);


    glitch.SetPitchSpreadType(
        sw2 ? 
        GlitchEngine::PitchSpreadType::PITCH_SPREAD_RAND : 
        GlitchEngine::PitchSpreadType::PITCH_SPREAD_OCTAVES
    );

    // update the glitch engine with the current parameters
    // float glitch_dur = linlin(knob_glitch_dur.Value(), 0.0f, 1.0f, 20.0f, 5000.0f);
    // float glitch_spread = knob_glitch_spread.Value();
    // float pitch = linlin(knob_pitch.Value(), 0.0f, 1.0f, -12.0f, 12.0f);
    // float pitch_spread = 0.0f;
    // float rskip = knob_rskip.Value();
    // float level = knob_level.Value();
    // float env_atk_amt = knob_env.Value();
    glitch.SetGlitchParams(
        /*glitch_dur=*/ glitch_dur,
        /*rskip=*/ rskip,
        /*glitch_spread=*/ glitch_spread,
        /*pitch=*/ pitch,
        /*pitch_spread=*/ pitch_spread,
        /*level=*/ level,
        /*env_atk_amt=*/ env_atk_amt
    );

    if (fsw1.rising) {
        glitch.clock().Reset();
        glitch.TriggerGlitch();
    }

    if (!fsw1.state) {
        glitch.StopGlitch();
    }


    if (takeover) { // notify user that the knob was taken over.
        ledw1.SetState(LedWrap::LedState::BLINK_SHORT, 500); // blink for 100ms on takeover
    } else if (fsw1.state && (ledw1.GetState() != LedWrap::LedState::BLINK_SHORT)) {
        ledw1.SetState(LedWrap::LedState::ON);
    } else if (!fsw1.state && (ledw1.GetState() != LedWrap::LedState::BLINK_SHORT)) {
        ledw1.SetState(LedWrap::LedState::OFF);
    }
    ledw1.Process();

    ledw2.SetState(LedWrap::LedState::BLINKING);
    ledw2.SetBlinkRate(glitch.clock().GetFreq()); 
    ledw2.Process();

}

/*
 * This runs at a fixed rate, to prepare audio samples
 */
float s_in[CHANS]; // temp buffer 
float s_out[CHANS]; // temp buffer for output
void callback(
    AudioHandle::InterleavingInputBuffer  in,
    AudioHandle::InterleavingOutputBuffer out,
    size_t                                size
    )
{
    hw.ProcessAllControls();
    processTerrariumControls();
    controlBlock();

    for(size_t i = 0; i < size; i += 2)
    {    
        // MONO!
        s_in[0] = in[i];
        // s_out[0] = 0.f; // initialize output to zero
            
        // Process the glitch engine
        glitch.ProcessOneFrame(s_in, s_out);

        out[i] = in[i] + s_out[0] * 4.0f;
        // out[i] = sample; // copy the input sample to the output buffer
    }
}

void PrintSignal(float* sig, size_t chans) {
    hw.seed.Print("(");
    for (size_t j = 0; j < chans; j++) {
        hw.seed.Print("%f,", sig[j]);
    }
    hw.seed.Print(")\t");
}

int main(void)
{
    hw.Init();
    sr = hw.AudioSampleRate();
    hw.seed.SetAudioBlockSize(BLOCK_SIZE);

    hw.seed.StartLog(false);

    led1.Init(hw.seed.GetPin(Terrarium::LED_1), false);
    led2.Init(hw.seed.GetPin(Terrarium::LED_2), false);
    ledw1.Init(led1, (int)sr / BLOCK_SIZE);
    ledw2.Init(led2, (int)sr / BLOCK_SIZE);

    
    // Initialize your knobs here like so:
    // https://electro-smith.github.io/libDaisy/classdaisy_1_1_parameter.html
    knob_glitch_dur     .Init(hw.knob[Terrarium::KNOB_1], 0.0f, 1.0f, Parameter::EXPONENTIAL);
    knob_glitch_spread  .Init(hw.knob[Terrarium::KNOB_2], 0.0f, 1.0f, Parameter::LINEAR);
    knob_pitch          .Init(hw.knob[Terrarium::KNOB_3], 0.0f, 1.0f, Parameter::LINEAR);
    knob_rskip          .Init(hw.knob[Terrarium::KNOB_4], 0.0f, 1.0f, Parameter::LINEAR);
    knob_level          .Init(hw.knob[Terrarium::KNOB_5], 0.0f, 1.0f, Parameter::LINEAR);
    knob_env            .Init(hw.knob[Terrarium::KNOB_6], 0.0f, 1.0f, Parameter::LINEAR);
    
    // init knob manager.
    skm.Init(6); // 6 knobs

    // Set samplerate for your processing like so:
    glitch.Init(sr, buf, BUF_SIZE, CHANS);
    
    hw.StartAdc();
    hw.StartAudio(callback);

    // print: hello! 
    // then, then number of channels in the main buffer, 

    int i = 0;
    while(1)
    {
        // Do lower priority stuff infinitely here
        System::Delay(400);
        // print sig
        // PrintSignal(s_in, CHANS);
        // // hw.seed.PrintLine("");
        // hw.seed.Print("-");
        // PrintSignal(s_out, CHANS);

        // print the LEDW states
        ledw1.PrintDebugState(hw);
        hw.seed.PrintLine("");
        ledw2.PrintDebugState(hw);
        hw.seed.PrintLine("");

        if (i % 4 == 0) {
            hw.seed.PrintLine("");
            glitch.PrintDebugState(hw);
            hw.seed.PrintLine("");
        }
        i++;
    }
}
