#include "daisy_petal.h"
#include "daisysp.h"
#include "terrarium.h"

using namespace daisy;
using namespace daisysp;
using namespace terrarium;

// Declare a global daisy_petal for hardware access
DaisyPetal  hw;

bool        fsw1, fsw2, sw1, sw2, sw3, sw4;
bool        fsw1_momentary, fsw2_momentary = false; // for temporary bypass mode
Led         led1, led2;

Parameter   knob1, 
            knob2, 
            knob3, 
            knob4, 
            knob5, 
            knob6;

float sr;

/*
 * Process terrarium knobs and switches
 */
void processTerrariumControls() {
    // update switch values
    // https://electro-smith.github.io/libDaisy/classdaisy_1_1_switch.html
    if(hw.switches[Terrarium::FOOTSWITCH_1].RisingEdge()) {
        fsw1 = !fsw1;
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
    knob1.Process();
    knob2.Process();
    knob3.Process();
    knob4.Process();
    knob5.Process();
    knob6.Process();


    led1.Set(fsw1 ? 0.0f : 1.0f);
    led2.Set(fsw2 ? 0.0f : 1.0f);

    hw.seed.PrintLine("FSW1: %d", fsw1);
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

    for(size_t i = 0; i < size; i += 2)
    {
        // Process your signal here
        if(!fsw1)
        {
            out[i] = in[i];
        }
        else
        {
            // processed signal
            // Using knob1 as volume here
            out[i] = in[i] * knob1.Value();
        }
    }
}

int main(void)
{
    hw.Init();
    sr = hw.AudioSampleRate();

    hw.seed.StartLog(false);

    led1.Init(hw.seed.GetPin(Terrarium::LED_1), false);
    led2.Init(hw.seed.GetPin(Terrarium::LED_2), false);

    // Initialize your knobs here like so:
    // https://electro-smith.github.io/libDaisy/classdaisy_1_1_parameter.html
    knob1.Init(hw.knob[Terrarium::KNOB_1], 0.0f, 0.999f, Parameter::LINEAR);
    knob2.Init(hw.knob[Terrarium::KNOB_2], 0.0f, 1.0f, Parameter::LINEAR);
    knob3.Init(hw.knob[Terrarium::KNOB_3], 0.0f, 1.0f, Parameter::LINEAR);
    knob4.Init(hw.knob[Terrarium::KNOB_4], 0.0f, 1.0f, Parameter::LINEAR);
    knob5.Init(hw.knob[Terrarium::KNOB_5], 0.0f, 1.0f, Parameter::LINEAR);
    knob6.Init(hw.knob[Terrarium::KNOB_6], 0.0f, 1.0f, Parameter::LINEAR);

    // Set samplerate for your processing like so:
    // verb.Init(samplerate);
    fsw1 = true;

    hw.StartAdc();
    hw.StartAudio(callback);

    while(1)
    {
        // Do lower priority stuff infinitely here
        System::Delay(10);
    }
}
