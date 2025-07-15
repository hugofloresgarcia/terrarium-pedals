#include "daisy_petal.h"
#include "daisysp.h"
#include "terrarium.h"

#include "lib/cenote_delay.h"
#include "lib/vibrato.h"

using namespace daisy;
using namespace daisysp;
using namespace terrarium;

// Declare a global daisy_petal for hardware access
DaisyPetal  hw;

bool        fsw_delay, fsw2, sw1, sw2, sw3, sw4;
bool        fsw2_momentary = false; // for temporary bypass mode
Led         led1, led2;
Parameter   knob_delaytime,
            knob_delayfb,
            knob_delayamt,
            knob_vibdepth,
            knob_vibrate,
            knob_shiftamt;

CenoteDelayEngine del;
VibratoEngine vibrato; // Two engines for stereo vibrato
// ReverbSc    verb;

/*
 * Process terrarium knobs and switches
 */
void processTerrariumControls() {
    // update footswitches
    if(hw.switches[Terrarium::FOOTSWITCH_1].RisingEdge()) {
        fsw_delay = !fsw_delay;
    }

    if(hw.switches[Terrarium::FOOTSWITCH_2].RisingEdge()) {
        fsw2 = !fsw2;
    }

    // TODO: fsw2 is held pressed for longer than 300ms, it is in temporary bypass mode
    // and should be disengaged after it is released
    if (hw.switches[Terrarium::FOOTSWITCH_2].Pressed() && 
            hw.switches[Terrarium::FOOTSWITCH_2].TimeHeldMs() > 300.0f) {
        fsw2_momentary = true;
    } else if (hw.switches[Terrarium::FOOTSWITCH_2].FallingEdge() && 
                fsw2_momentary) {
        fsw2_momentary = false;
        fsw2 = false; // disengage bypass mode
    }


    sw1 = hw.switches[Terrarium::SWITCH_1].Pressed();
    sw2 = hw.switches[Terrarium::SWITCH_2].Pressed();
    sw3 = hw.switches[Terrarium::SWITCH_3].Pressed();
    sw4 = hw.switches[Terrarium::SWITCH_4].Pressed();

    // update knob values
    // https://electro-smith.github.io/libDaisy/classdaisy_1_1_analog_control.html
    knob_delaytime.Process();
    knob_delayfb.Process();
    knob_delayamt.Process();
    knob_vibdepth.Process();
    knob_vibrate.Process();
    knob_shiftamt.Process();

    led1.Set(fsw_delay ? 1.0f : 0.0f);
    led2.Set(fsw2 ? 1.0f : 0.0f);

    // set knob_delaytime to delay time
    float delay_time_mult = sw3 ? 1.0f : 0.1f; // 0.5x if sw3 is pressed, otherwise 1.0x
    del.SetDelayMs(knob_delaytime.Value() * del.GetMaxDelayMs() * delay_time_mult); //

    // set knob_delayfb to feedback
    del.SetFeedback(
        fsw2 ? 1.0f : // fsw2 is full feedback mode.
            knob_delayfb.Value()*0.9999999999f

    );

    // set knob_shiftamt to pitch shift amount
    float up_or_down = sw4 ? 1.0f : -1.0f; // negative if sw4 is pressed
    float shift_mult = sw3 ? 150.0f : 15.0f; // 150 hz if sw3 is pressed, otherwise 15 hz
    del.SetTransposition(up_or_down * (knob_shiftamt.Value() * shift_mult));
    del.SetBypassFrequencyShift(!sw2); // bypass frequency shifting if sw2 is pressed

    // set knob_vibdepth to vibrato depth
    // if sw1 is pressed, set it to "extreme" depth
    // otherwise, set it to a fraction of the knob value
    vibrato.SetLfoDepth(
        sw1 ? 1.0 : knob_vibdepth.Value() * 0.25f
    );

    // set knob_vibrate to vibrato frequency
    vibrato.SetLfoFreq(knob_vibrate.Value() * 15.0f);
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

    for(size_t i = 0; i < size; i += 2)
    {
        sig = in[i];

        // vibrato is always on
        sig = vibrato.Process(sig);
        
        // process!
        if(fsw_delay || fsw2)
        {
            // process delay
            del_out = del.Process(sig, /*clip=*/true, /*limit=*/fsw2);
            
            // mix delay
            out[i] = sig + del_out * knob_delayamt.Value();
            
            // softclip out
            out[i] = SoftClip(out[i]); // Soft clipping
        }
        else
        {
            out[i] = sig;
        }
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
    knob_delayfb.Init(hw.knob[Terrarium::KNOB_3], 0.0f, 1.0f, Parameter::LINEAR);
    knob_delayamt.Init(hw.knob[Terrarium::KNOB_6], 0.0f, 1.0f, Parameter::LINEAR);
    knob_vibrate.Init(hw.knob[Terrarium::KNOB_1], 0.0f, 1.0f, Parameter::LINEAR);
    knob_vibdepth.Init(hw.knob[Terrarium::KNOB_4], 0.0f, 1.0f, Parameter::EXPONENTIAL);
    knob_shiftamt.Init(hw.knob[Terrarium::KNOB_5], 0.0f, 1.0f, Parameter::LINEAR);

    // Set sr for your processing like so:
    // verb.Init(sr);
    del.Init(sr);
    vibrato.Init(sr);

    // bypass = false;

    hw.StartAdc();
    hw.StartAudio(callback);

    while(1)
    {
        // Do lower priority stuff infinitely here
        System::Delay(10);
    }
}
