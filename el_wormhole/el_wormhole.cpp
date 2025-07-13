#include "daisy_petal.h"
#include "daisysp.h"
#include "terrarium.h"
#include "delay.h"
#include "vibrato.h"

using namespace daisy;
using namespace daisysp;
using namespace terrarium;

// Declare a global daisy_petal for hardware access
DaisyPetal  hw;

bool        fsw_delay, fsw2, sw1, sw2, sw3, sw4;
Led         led1, led2;
Parameter   knob_delaytime,
            knob_delayfb,
            knob_delayamt,
            knob_vibdepth,
            knob_vibrate,
            knob_shiftamt;

HDelayEngine del;
VibratoEngine vibrato; // Two engines for stereo vibrato
// ReverbSc    verb;

/*
 * Process terrarium knobs and switches
 */
void processTerrariumControls() {
    // update switch values
    // https://electro-smith.github.io/libDaisy/classdaisy_1_1_switch.html
    // resolve footswitches
    if(hw.switches[Terrarium::FOOTSWITCH_1].RisingEdge()) {
        fsw_delay = !fsw_delay;
    }
    if(hw.switches[Terrarium::FOOTSWITCH_2].RisingEdge()) {
        fsw2 = !fsw2;
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

    led1.Set(fsw_delay ? 0.0f : 1.0f);
    led2.Set(!fsw_delay && fsw2 ? 1.0f : 0.0f);

    // set knob_delaytime to delay time
    float delay_time_mult = sw3 ? 1.0f : 0.125f; // 0.5x if sw3 is pressed, otherwise 1.0x
    del.SetDelayMs(knob_delaytime.Value() * del.GetMaxDelayMs() * delay_time_mult); //

    // set knob_delayfb to feedback
    del.SetFeedback(
        fsw2 ? 0.9999f :
            knob_delayfb.Value()*0.99f

    );

    // set knob_vibdepth to vibrato depth
    // vibrato.SetMaxDelayMs();
    vibrato.SetLfoDepth(
        sw1 ? 1.0 : knob_vibdepth.Value() * 0.25f
    );

    // set knob_vibrate to vibrato frequency
    vibrato.SetLfoFreq(knob_vibrate.Value() * 15.0f);

    // set knob_shiftamt to pitch shift amount
    float up_or_down = sw4 ? 1.0f : -1.0f; // negative if sw4 is pressed
    float shift_mult = sw3 ? 100.0f : 15.0f; // 100 hz if sw3 is pressed, otherwise 15 hz
    del.SetTransposition(up_or_down * (knob_shiftamt.Value() * shift_mult));
    del.SetBypassFrequencyShift(!sw2); // bypass frequency shifting if sw2 is pressed
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
        sig = in[i]; // left channel
        sig = vibrato.Process(sig);
        
        // Process your signal here
        if(fsw_delay)
        {
            out[i] = sig;
        }
        else
        {
            // sig = pitchshifter.Process(sig);
            del_out = del.Process(sig);

            out[i] = sig + del_out * knob_delayamt.Value();

            out[i] = SoftClip(out[i]); // Soft clipping
        }
    }
}

int main(void)
{
    float samplerate;

    hw.Init();
    samplerate = hw.AudioSampleRate();

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

    // Set samplerate for your processing like so:
    // verb.Init(samplerate);
    del.Init(samplerate);
    vibrato.Init(samplerate);

    // bypass = false;

    hw.StartAdc();
    hw.StartAudio(callback);

    while(1)
    {
        // Do lower priority stuff infinitely here
        System::Delay(10);
    }
}
