#include <JuceHeader.h>
#include "SandboxedPedal.h"  // includes ../cenote/cenote.cpp with BUILDING_FOR_EMULATOR

// from cenote.cpp
extern daisy::DaisyPetal hw;

struct SwitchSimState {
    bool pressed = false;
    double lastChangeMs = 0.0;
    float timeHeldMs = 0.0f;
    bool rising = false;
    bool falling = false;

    void update(bool isDown, double nowMs)
    {
        rising  = (!pressed && isDown);
        falling = (pressed && !isDown);
        if (rising) lastChangeMs = nowMs;
        pressed = isDown;
        timeHeldMs = pressed ? (float)(nowMs - lastChangeMs) : 0.0f;
    }
};

class LedWidget : public juce::Component
{
public:
    void setColour(juce::Colour c) { colour = c; repaint(); }
    void setLevel(float v)
    {
        v = juce::jlimit(0.0f, 1.0f, v);
        if (std::abs(v - level) > 1e-4f) { level = v; repaint(); }
    }

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(2.0f);
        // brighten with level; keep some base glow so OFF is still visible slightly if you want
        auto fill = colour.withMultipliedBrightness(0.2f + 0.8f * level)
                           .withAlpha(0.7f + 0.3f * level);
        g.setColour(fill);
        g.fillEllipse(r);

        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.drawEllipse(r, 1.5f);
    }

private:
    float level = 0.0f;
    juce::Colour colour = juce::Colours::red;
};

class MomentaryButton : public juce::TextButton
{
public:
    explicit MomentaryButton (const juce::String& nm, std::function<void(bool)> cb)
        : juce::TextButton(nm), onHoldChange(std::move(cb)) {}

private:
    void buttonStateChanged() override
    {
        // getState() is a bitfield – check the buttonDown bit
        const bool isDown = (getState() & juce::Button::buttonDown) != 0;
        if (onHoldChange) onHoldChange(isDown);
    }

    std::function<void(bool)> onHoldChange;
};


class MainComponent : public juce::Component,
                      public juce::AudioIODeviceCallback,
                      private juce::Timer
{
public:
    MainComponent()
    {
        // --- Knobs
        std::cout<<"here"<<std::endl;
        static const char* names[6] = { "Rate", "Delay", "Feedback", "Vib Depth", "Shift", "Level" };
        for (int i = 0; i < 6; ++i)
        {
            auto s = std::make_unique<juce::Slider>();
            s->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
            s->setRange(0.0, 1.0, 0.0001);
            s->setValue(0.5);
            addAndMakeVisible(*s);

            auto l = std::make_unique<juce::Label>();
            l->setJustificationType(juce::Justification::centred);
            l->setText(names[i], juce::dontSendNotification);
            addAndMakeVisible(*l);

            knobs.add(s.release());
            knobLabels.add(l.release());
        }

        // --- Switches SW1..SW4 (toggle)
        for (int i = 0; i < 4; ++i)
        {
            auto* b = switches.add(new juce::ToggleButton(juce::String("SW") + juce::String(i+1)));
            addAndMakeVisible(*b);
        }

        // --- Footswitches FS1/FS2 (momentary)
        footswitches.add(new MomentaryButton("FS1 (hold)", [this](bool down){ fsDown[0] = down; }));
        footswitches.add(new MomentaryButton("FS2 (hold)", [this](bool down){ fsDown[1] = down; }));
        addAndMakeVisible(*footswitches[0]);
        addAndMakeVisible(*footswitches[1]);

        // --- Audio Settings button (opens device selector)
        audioSettingsBtn.setButtonText("Audio Settings");
        audioSettingsBtn.onClick = [this]
        {
            auto content = std::make_unique<juce::AudioDeviceSelectorComponent>(
                deviceManager,
                /*minInputs*/  0, /*maxInputs*/  2,
                /*minOutputs*/ 0, /*maxOutputs*/ 2,
                /*showMidiIn*/ false, /*showMidiOut*/ false,
                /*stereoPairs*/ true,
                /*hideAdvanced*/ false
            );
            content->setSize(520, 360);

            juce::CallOutBox::launchAsynchronously(
                std::move(content),
                audioSettingsBtn.getScreenBounds(),
                this
            );
        };

        addAndMakeVisible(audioSettingsBtn);

        addAndMakeVisible(ledUI1);
        addAndMakeVisible(ledUI2);
        ledUI1.setColour(juce::Colours::chartreuse);
        ledUI2.setColour(juce::Colours::orange);

        setSize(900, 420);

        // Initialise your pedal (cenote.cpp -> sandbox::setup)
        sandbox::setup();

        // --- Open audio device: default 1 in / 1 out (user can change in selector)
        juce::String error = deviceManager.initialise(1, 1, nullptr, true);
        jassert(error.isEmpty()); juce::ignoreUnused(error);

        deviceManager.addAudioCallback(this);

        // Control-rate tick ~60 Hz
        startTimerHz(60);
    }

    
    ~MainComponent() override
    {
        deviceManager.removeAudioCallback(this);
        selectorHolder.reset();
    }

    static void postSwitch(SwitchSimState& sim, daisy::Switch& hwSw)
    {
        hwSw.pressed  = sim.pressed;
        hwSw.timeHeld = sim.timeHeldMs;

        // Only set to true; DSP clears them by calling RisingEdge/FallingEdge
        if (sim.rising)  hwSw.rising  = true;
        if (sim.falling) hwSw.falling = true;
    }

    // ======== Audio IO ========
    void audioDeviceAboutToStart (juce::AudioIODevice* /*device*/) override {}
    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                int numInputChannels,
                                float* const* outputChannelData,
                                int numOutputChannels,
                                int numSamples,
                                const juce::AudioIODeviceCallbackContext& context) override
    {
        auto* outL = numOutputChannels > 0 ? outputChannelData[0] : nullptr;
        const float* inL = numInputChannels > 0 ? inputChannelData[0] : nullptr;

        if (!outL) return;

        for (int i = 0; i < numSamples; ++i)
        {
            float in = inL ? inL[i] : 0.0f;
            float y  = 0.0f;
            sandbox::audio(&in, &y, 1);
            outL[i] = y;
        }

        // clear any extra output channels
        for (int ch = 1; ch < numOutputChannels; ++ch)
            if (auto* out = outputChannelData[ch]) juce::FloatVectorOperations::clear(out, numSamples);
    }


    void timerCallback() override
    {
        using namespace terrarium;

        auto setKnob = [] (daisy::AnalogControl& ac, float v) { ac.val = v; };
        setKnob(hw.knob[Terrarium::KNOB_1], (float)knobs[0]->getValue());
        setKnob(hw.knob[Terrarium::KNOB_2], (float)knobs[1]->getValue());
        setKnob(hw.knob[Terrarium::KNOB_3], (float)knobs[2]->getValue());
        setKnob(hw.knob[Terrarium::KNOB_4], (float)knobs[3]->getValue());
        setKnob(hw.knob[Terrarium::KNOB_5], (float)knobs[4]->getValue());
        setKnob(hw.knob[Terrarium::KNOB_6], (float)knobs[5]->getValue());

        const double nowMs = juce::Time::getMillisecondCounterHiRes();

        // update GUI sim states
        for (int i = 0; i < 4; ++i)
            simSW[i].update(switches[i]->getToggleState(), nowMs);
        fsSim[0].update(fsDown[0], nowMs);
        fsSim[1].update(fsDown[1], nowMs);

        // post to hardware (only set true edges; DSP clears them)
        postSwitch(simSW[0], hw.switches[Terrarium::SWITCH_1]);
        postSwitch(simSW[1], hw.switches[Terrarium::SWITCH_2]);
        postSwitch(simSW[2], hw.switches[Terrarium::SWITCH_3]);
        postSwitch(simSW[3], hw.switches[Terrarium::SWITCH_4]);

        postSwitch(fsSim[0], hw.switches[Terrarium::FOOTSWITCH_1]);
        postSwitch(fsSim[1], hw.switches[Terrarium::FOOTSWITCH_2]);

        // (optional debug)
        // if (fsSim[0].rising)  DBG("FS1 rising (UI)");
        // if (fsSim[0].falling) DBG("FS1 falling (UI)");
        
        // print all the footswitch states for hw.switches
        // std::cout<<"FS1: pressed=" << hw.switches[Terrarium::FOOTSWITCH_1].pressed
        //     << " timeHeld=" << hw.switches[Terrarium::FOOTSWITCH_1].timeHeld
        //     << " rising=" << hw.switches[Terrarium::FOOTSWITCH_1].rising
        //     << " falling=" << hw.switches[Terrarium::FOOTSWITCH_1].falling<<std::endl;
        // std::cout<<"FS2: pressed=" << hw.switches[Terrarium::FOOTSWITCH_2].pressed
        //     << " timeHeld=" << hw.switches[Terrarium::FOOTSWITCH_2].timeHeld
        //     << " rising=" << hw.switches[   Terrarium::FOOTSWITCH_2].rising
        //     << " falling=" << hw.switches[Terrarium::FOOTSWITCH_2].falling<<std::endl;



        // run the pedal’s control loop (which consumes RisingEdge/FallingEdge)
        sandbox::control();
    }


    void resized() override
    {
        auto area = getLocalBounds().reduced(10);

        // top: audio settings
        auto top = area.removeFromTop(32);
        audioSettingsBtn.setBounds(top.removeFromLeft(160));

        // show LEDs on the top right
        auto ledBox = top.removeFromRight(80);
        ledUI1.setBounds(ledBox.removeFromLeft(36).reduced(6));
        ledUI2.setBounds(ledBox.removeFromLeft(36).reduced(6));

        // knobs
        auto row = area.removeFromTop(160);
        auto cellW = row.getWidth() / 6;
        for (int i = 0; i < 6; ++i)
        {
            auto cell = row.removeFromLeft(cellW).reduced(8);
            knobLabels[i]->setBounds(cell.removeFromTop(20));
            knobs[i]->setBounds(cell);
        }

        // switches
        auto mid = area.removeFromTop(60);
        auto swCell = mid.getWidth() / 6;
        for (int i = 0; i < 4; ++i)
            switches[i]->setBounds(mid.removeFromLeft(swCell).reduced(8));

        // footswitches
        auto bot = area;
        auto fsW = bot.getWidth() / 2;
        footswitches[0]->setBounds(bot.removeFromLeft(fsW).reduced(8));
        footswitches[1]->setBounds(bot.removeFromLeft(fsW).reduced(8));
    }

private:
    juce::AudioDeviceManager deviceManager;

    juce::OwnedArray<juce::Slider>        knobs;
    juce::OwnedArray<juce::Label>         knobLabels;
    juce::OwnedArray<juce::ToggleButton>  switches;
    juce::OwnedArray<MomentaryButton>     footswitches;

    juce::TextButton audioSettingsBtn;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> selectorHolder;

    bool fsDown[2] { false, false };
    SwitchSimState simSW[4];
    SwitchSimState fsSim[2];
    LedWidget ledUI1, ledUI2;
};

// -------- App boilerplate --------
class SandboxApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName()    override { return "Daisy Sandbox"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    void initialise (const juce::String&) override { win.reset(new W(getApplicationName())); }
    void shutdown() override { win = nullptr; }

private:
    struct W : juce::DocumentWindow {
        W (juce::String name) : DocumentWindow (name, juce::Colours::darkgrey, allButtons)
        {
            setUsingNativeTitleBar(true);
            setResizable(true, true);
            setContentOwned(new MainComponent(), true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<W> win;
};

START_JUCE_APPLICATION (SandboxApp)
