#include "daisy_petal.h"
#include "daisysp.h"
#include "terrarium.h"

#include "lib/glitch.h"
#include "ledwrap.h"
#include "shiftknobman.h"
#include "fsw.h"
#include "fmath.h"
#include "knob.h"
#include "xfade.h"
#include "taptempo.h"


#define BUF_SIZE (48000 * 10)  // 10 seconds of audio at 48kHz
#define CHANS 1                // mono :(
#define BLOCK_SIZE 4            // 4 samples per block for audio processing

using namespace daisy;
using namespace daisysp;
using namespace terrarium;

// **************************************************
// HARDWARE
// **************************************************

// globals
DaisyPetal  hw;
float sr; // sample rate

// hardware
FswState   fsw1, fsw2; // footswitch states
bool        sw1, sw2, sw3, sw4;
Led         led1, led2;
LedWrap     ledw1, ledw2;

// knobs
FKnob      knob_glitch_dur, 
            knob_glitch_spread, 
            knob_pitch, 
            knob_rskip, 
            knob_level, 
            knob_env;

MoogLadder filter; // lowpass filter for smoothing out the glitch output

// knob indices 
enum KNOB {
    KNOB_GLITCH_DUR = 0,
    KNOB_GLITCH_SPREAD,
    KNOB_PITCH,
    KNOB_PATTERN,
    KNOB_LEVEL,
    KNOB_ENV,
    KNOB_LAST
};
// manager for shift-functions for knobs
ShiftKnobManager skm;

// tap tempo
TapTempo tap_tempo;

// the glitch engine
GlitchEngine glitch;

// xfade
Xfade xfade;
Limiter limiter;

// our buffer, for the glitch engine
float DSY_SDRAM_BSS buf[BUF_SIZE * CHANS];

// **************************************************
// SETTINGS
// **************************************************

// // only save the "shift knob" settings to persist.
// struct GlitchSettings {
//     float shift_knob_glitch_dur = 0.0f; // Glitch duration
//     float shift_knob_glitch_spread = 0.0f; // Glitch spread
//     float shift_knob_pitch = 0.0f; // Pitch
//     float shift_knob_rskip = 0.0f; // Random skip
//     float shift_knob_level = 0.0f; // Level
//     float shift_knob_env = 0.0f; // Envelope amount

//     bool operator!=(const GlitchSettings& other) const {
//         return !(other.shift_knob_glitch_dur == shift_knob_glitch_dur &&
//                   other.shift_knob_glitch_spread == shift_knob_glitch_spread &&
//                   other.shift_knob_pitch == shift_knob_pitch &&
//                   other.shift_knob_rskip == shift_knob_rskip &&
//                   other.shift_knob_level == shift_knob_level &&
//                   other.shift_knob_env == shift_knob_env);
//     }
// };

// PersistentStorage<GlitchSettings> saved_settings(hw.seed.qspi);
// GlitchSettings settings;
// bool trigger_save = false;

// **************************************************
// CONTROL RATE: FOOTSWITCHES
// **************************************************

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
        // if (fsw1.state) {fsw2.state = false;} // if fsw1 is pressed, disengage fsw2
    }
    
    if(fsw2.rising) {
        fsw2.state = !fsw2.state;
        // if (fsw2.state) {fsw1.state = false;} // if fsw2 is pressed, disengage fsw1
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


// **************************************************
// CONTROL RATE: CONTROLS
// **************************************************

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

// **************************************************
// CONTROL RATE: CONTROL BLOCK
// **************************************************
float glitch_dur_ = 0;
bool tapped = false;
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
    
    skm.SetShift(fsw2.momentary);
    bool takeover = skm.ProcessKnobs(hw_knobs);

    // figure out if we are operating in tap (or free tempo)
    bool is_tap_mode = sw1;
    tap_tempo.Process();
    if (fsw2.rising) {
        tapped = true;
        tap_tempo.Tap();
        glitch.clock().Reset();
    }

    // GLITCH MATH!!!!!
    // how long will glitch be? 
    float glitch_dur = glitch_dur_;
    if (is_tap_mode) {
        glitch_dur_ = tap_tempo.GetPeriodMs();

        float mult;
        float mult_vals[6] = {1.0, 0.25, 0.5, 1.0, 2.0, 4.0};
        mult = mult_vals[(int)(skm.GetNormalValue(KNOB_GLITCH_DUR) * 6)];

        glitch_dur = mult * glitch_dur_;
    } else {
        glitch_dur_ = linlin(
            skm.GetNormalValue(KNOB_GLITCH_DUR), 
            0.0f, 1.0f, 80.0f, 1000.0f
        );

        // // add a random amount to the glitch duration of up to 50% of the duration
        // float rand_dur_amt = skm.GetShiftValue(KNOB_GLITCH_DUR);
        // float rand_dur = rand_dur_amt * randf(-0.5f * glitch_dur, 0.5f * glitch_dur);
        // glitch_dur += rand_dur;
        // glitch_dur = fclamp(glitch_dur, 80.f, 1000.f); // clamp between 20ms and 5000ms
        tap_tempo.SetPeriodMs(glitch_dur); // update the tap tempo period to match the knob value
    }

    // glitch spread. 
    float glitch_mem = skm.GetNormalValue(KNOB_GLITCH_SPREAD);
    float glitch_spread = skm.GetShiftValue(KNOB_GLITCH_SPREAD);

    glitch.SetGlitchMemory(glitch_mem);

    // glitch pitch. 
    float pitch = linlin(
        skm.GetNormalValue(KNOB_PITCH), 
        0.0f, 1.0f, -24.0f, 24.0f
    );
    if (sw4) {
        // round pitch to the nearest semitone
        pitch = roundf(pitch); 
    } else {
        // round pitch to nearest octave
        pitch = roundf(pitch / 12.0f) * 12.0f;
    }
    
    float pitch_spread = skm.GetShiftValue(KNOB_PITCH) * 24.0f; // up to +/- 12 semitones of pitch spread
    pitch_spread = fclamp(pitch_spread, 0.f, 24.f); // clamp between 0 and 12 semitones

    // random skip
    float rskip = skm.GetShiftValue(KNOB_PATTERN);

    // level also acts as a bypass switch
    xfade.SetCrossfade(
        fsw1.state ? 
        skm.GetNormalValue(KNOB_LEVEL)
        : 0.0f
    );

    // float level = skm.GetNormalValue(KNOB_LEVEL); // now a xfade
    float env_atk_amt = skm.GetNormalValue(KNOB_ENV);
    float overlap = linlin(
        skm.GetShiftValue(KNOB_ENV), 
        0.0f, 1.0f, 0.1f, 4.0f
    ); // overlap is a percentage of the glitch duration

    // PATTERN CONFIG
    uint8_t max_pattern_len = sw2 ? 32 : 16; 
    glitch.SetPatternLength(
        (size_t)(skm.GetNormalValue(KNOB_PATTERN) * (max_pattern_len + 1 ))
    );


    // reset pattern if our knobs move
    if (knob_glitch_dur.Moved() ||
            knob_glitch_spread.Moved() ||
            knob_pitch.Moved() ||
            knob_rskip.Moved() ||
            knob_env.Moved()) {
        glitch.ResetPattern();
    }

    // CONFIGURE GLITCH  
    glitch.SetPitchSpreadType(
        sw4 ? 
        GlitchEngine::PitchSpreadType::PITCH_SPREAD_RAND : 
        GlitchEngine::PitchSpreadType::PITCH_SPREAD_OCTAVES
    );
    glitch.SetGlitchParams(
        /*glitch_dur=*/ glitch_dur,
        /*rskip=*/ rskip,
        /*glitch_spread=*/ glitch_spread,
        /*pitch=*/ pitch,
        /*pitch_spread=*/ pitch_spread,
        /*level=*/ 1.0,
        /*env_atk_amt=*/ env_atk_amt,
        /*freeze=*/ !sw3, // freeze the buffer if the footswitch is pressed
        /*overlap=*/ overlap // overlap is a percentage of the glitch duration,
    );


    // configure filter
    filter.SetRes(0.6f);
    filter.SetFreq(linlin(skm.GetShiftValue(KNOB_LEVEL), 0.0, 1.0, 100.0, 8000.0f));


    // TRIGGER GLITCH!
    if (fsw1.rising) {
        glitch.clock().Reset();
        // TODO: should we delay this by a "dur" cycle, so that the audio when you step on the footswitch is not cut off?
        // TODO: chatgpt, figure out a nonblocking way to do this. I'm thinking a class 
        // that is capable of delaying a trigger by a certain amount of time, and then calling a callback function.
        // as long as it gets called at the audio rate. 
        glitch.TriggerGlitch();
    }


    // STOP GLITCH? 
    if (!fsw1.state) {
        glitch.StopGlitch();
    }

    // LED CONFIGS!
    if (takeover) { // notify user that the knob was taken over.
        ledw1.SetState(LedWrap::LedState::BLINK_SHORT, 500); // blink for 100ms on takeover
    } else if (fsw1.state && (ledw1.GetState() != LedWrap::LedState::BLINK_SHORT)) {
        ledw1.SetState(LedWrap::LedState::ON);
    } else if (!fsw1.state && (ledw1.GetState() != LedWrap::LedState::BLINK_SHORT)) {
        ledw1.SetState(LedWrap::LedState::OFF);
    }
    ledw1.Process();

    if (fsw2.momentary) { // on while in settings mode. 
        ledw2.SetState(LedWrap::LedState::ON);
    } else {
        ledw2.SetState(LedWrap::LedState::BLINKING);
        ledw2.SetBlinkRate(glitch.clock().GetFreq()); 
    }
    ledw2.Process();
}

// **************************************************
// SETTINGS: SAVE AND LOAD
// **************************************************

// void Load() {
//     GlitchSettings &loaded_settings = saved_settings.GetSettings();

//     skm.SetShiftValue(KNOB_GLITCH_DUR, loaded_settings.shift_knob_glitch_dur);
//     skm.SetShiftValue(KNOB_GLITCH_SPREAD, loaded_settings.shift_knob_glitch_spread);
//     skm.SetShiftValue(KNOB_PITCH, loaded_settings.shift_knob_pitch);
//     skm.SetShiftValue(KNOB_PATTERN, loaded_settings.shift_knob_rskip);
//     skm.SetShiftValue(KNOB_LEVEL, loaded_settings.shift_knob_level);
//     skm.SetShiftValue(KNOB_ENV, loaded_settings.shift_knob_env);
// }

// void Save() {
//     GlitchSettings &stored_settings = saved_settings.GetSettings();

//     stored_settings.shift_knob_glitch_dur = skm.GetShiftValue(KNOB_GLITCH_DUR);
//     stored_settings.shift_knob_glitch_spread = skm.GetShiftValue(KNOB_GLITCH_SPREAD);
//     stored_settings.shift_knob_pitch = skm.GetShiftValue(KNOB_PITCH);
//     stored_settings.shift_knob_rskip = skm.GetShiftValue(KNOB_PATTERN);
//     stored_settings.shift_knob_level = skm.GetShiftValue(KNOB_LEVEL);
//     stored_settings.shift_knob_env = skm.GetShiftValue(KNOB_ENV);

//     trigger_save = true; // set flag to save in main loop
// }

// **************************************************
// AUDIO RATE: CALLBACK
// **************************************************

/*
 * This runs at a fixed rate, to prepare audio samples
 */
float s_in[CHANS]; // input signal 
float s_out[CHANS]; // output signal
float glitch_out[CHANS]; // glitch output

void callback(
    AudioHandle::InterleavingInputBuffer  in,
    AudioHandle::InterleavingOutputBuffer out,
    size_t                                size
    )
{
    hw.ProcessAllControls();
    processTerrariumControls();
    controlBlock();
    // Save(); // save settings if needed

    for(size_t i = 0; i < size; i += 2)
    {    
        // MONO!
        s_in[0] = in[i];
        // s_out[0] = 0.f; // initialize output to zero
            
        // Process the glitch engine
        glitch.ProcessFrame(s_in, glitch_out);

        // if (skm.GetShiftValue(KNOB_LEVEL) < 0.97f) {
        glitch_out[0] = filter.Process(glitch_out[0] * 12.0f);// MONO!!! add a little boost pre-filter
        // }

        limiter.ProcessBlock(glitch_out, CHANS, 1.0);
        
        xfade.ProcessFrame(s_in, glitch_out, s_out);

        out[i] = s_out[0];
        // out[i] = s_in[0] + glitch_out[0];
        // out[i] = sample; // copy the input sample to the output buffer
    }
}

// **************************************************
// DEBUGGING
// **************************************************
void PrintSignal(float* sig, size_t chans) {
    hw.seed.Print("(");
    for (size_t j = 0; j < chans; j++) {
        hw.seed.Print("%f,", sig[j]);
    }
    hw.seed.Print(")\t");
}


// **************************************************
// MAIN ENTRYPOINT
// **************************************************

int main(void)
{
    hw.Init();
    hw.seed.StartLog(false);

    // print the qspi status
    hw.seed.PrintLine("QSPI Status: %d", hw.seed.qspi.GetStatus());
    
    sr = hw.AudioSampleRate();
    hw.seed.SetAudioBlockSize(BLOCK_SIZE);

    hw.seed.PrintLine("Hello! Glitch Pedal Initialized with %d channels at %d Hz", CHANS, (int)sr);
    led1.Init(hw.seed.GetPin(Terrarium::LED_1), false);
    led2.Init(hw.seed.GetPin(Terrarium::LED_2), false);
    ledw1.Init(led1, (int)sr / BLOCK_SIZE);
    ledw2.Init(led2, (int)sr / BLOCK_SIZE);
    
    // Initialize your knobs here like so:
    // https://electro-smith.github.io/libDaisy/classdaisy_1_1_parameter.html
    knob_glitch_dur     .Init(hw.knob[Terrarium::KNOB_1], 0.0f, 1.0f, Parameter::EXPONENTIAL, sr);
    knob_glitch_spread  .Init(hw.knob[Terrarium::KNOB_2], 0.0f, 1.0f, Parameter::LINEAR, sr);
    knob_pitch          .Init(hw.knob[Terrarium::KNOB_3], 0.0f, 1.0f, Parameter::LINEAR, sr);
    knob_rskip          .Init(hw.knob[Terrarium::KNOB_4], 0.0f, 1.0f, Parameter::LINEAR, sr);
    knob_level          .Init(hw.knob[Terrarium::KNOB_5], 0.0f, 1.0f, Parameter::EXPONENTIAL, sr);
    knob_env            .Init(hw.knob[Terrarium::KNOB_6], 0.0f, 1.0f, Parameter::LINEAR, sr);
    
    hw.seed.PrintLine("Initializing stuff");

    // saved_settings.Init(settings, /*address_offset=*/ 256);
    hw.seed.PrintLine("Initialized persistent storage");


    // // init knob manager.
    skm.Init(6); // 6 knobs
    hw.seed.PrintLine("Initialized shift knob manager");

    tap_tempo.Init((int)(sr / BLOCK_SIZE));
    
    // // set default overlap to 1.0f 
    float target_default_overlap_value = 1.0f;
    float target_default_overlap_knob_value = linlin(target_default_overlap_value, 0.1f, 4.0f, 0.1f, 1.0f);
    skm.SetShiftValue(KNOB_ENV, target_default_overlap_knob_value);

    // skm.SetShiftValue(KNOB_GLITCH_SPREAD, 1.0); // init to full spread

    skm.SetShiftValue(KNOB_LEVEL, 1.0f); // start with filter fully open. 


    // Set samplerate for your processing like so:
    glitch.Init(sr, buf, BUF_SIZE, CHANS);
    hw.seed.PrintLine("Initialized glitch engine with buffer size %d and %d channels", BUF_SIZE, CHANS);
    
    xfade.Init(sr, CHANS, 10.0f);
    xfade.SetCrossfadeType(Xfade::TYPE::EQ_POWER); // default to power crossfade
    hw.seed.PrintLine("Initialized xfade with %d channels", CHANS);

    limiter.Init();

    filter.Init(sr);

    hw.StartAdc();
    hw.StartAudio(callback);

    // print: hello! 
    // then, then number of channels in the main buffer, 
    // load saved settings
    // Load();
    // hw.seed.PrintLine("Loaded saved settings");

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
        // ledw1.PrintDebugState(hw);
        // hw.seed.PrintLine("");
        // ledw2.PrintDebugState(hw);
        // hw.seed.PrintLine("");

        if (i % 2 == 0) {
            hw.seed.PrintLine("");
            glitch.PrintDebugState(hw);
            hw.seed.PrintLine("");
            hw.seed.PrintLine("");
            // hw.seed.PrintLine("Tap Tempo ms %f", tap_tempo.GetPeriodMs());
            // hw.seed.PrintLine("tapped; %d", tapped);
            // if (tapped) { tapped = false; }
            // tap_tempo.PrintDebugState(hw);
        }
        i++;
        // // print if each knob has moved
        // knob_glitch_dur.PrintDebug(hw);
        // knob_glitch_spread.PrintDebug(hw);
        // knob_pitch.PrintDebug(hw);
        // knob_rskip.PrintDebug(hw);
        // knob_level.PrintDebug(hw);
        // knob_env.PrintDebug(hw);

        // if(trigger_save) {
        //     // saved_settings.Save(); // Writing locally stored settings to the external flash
        //     trigger_save = false;
        // }
        // System::Delay(1000);
    }


}
