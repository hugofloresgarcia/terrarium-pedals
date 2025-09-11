#pragma once
#include <array>

#define DSY_SDRAM_BSS
namespace daisy {

class Switch {
public:
    // set by GUI thread
    bool pressed = false;
    bool rising = false, falling = false;
    float timeHeld = 0.f;

    // read by pedal code
    bool Pressed() const { return pressed; }

    // one-shot semantics (return true once, then clear)
    bool RisingEdge()  { bool r = rising;  rising  = false; return r; }
    bool FallingEdge() { bool f = falling; falling = false; return f; }

    float TimeHeldMs() const { return timeHeld; }
};

class AnalogControl {
public:
    float val = 0.0f;                   // <- we write this from JUCE
    void Init(...) {}
    float Value() const { return val; }
    float Process()     { return val; } // return current value
    float GetRawFloat() const { return val; } // return current value
};

class Parameter {
public:
    enum Curve { LINEAR, EXPONENTIAL, LOGARITHMIC, CUBE };

    // Match your usage: knob.Init(hw.knob[idx], min, max, curve [, sr])
    void Init(AnalogControl& ac, float min, float max, Curve curve, float sr = 48000.0f)
    {
        pmin_   = min;
        pmax_   = max;
        pcurve_ = curve;
        ac_     = &ac;
        lmin_   = logf(min < 0.0000001f ? 0.0000001f : min);
        lmax_   = logf(max);

        // ensure AnalogControl smoothing is set up
        ac.Init(sr);
    }

    float Process()
        {
        switch(pcurve_)
        {
            case LINEAR: val_ = (ac_->Process() * (pmax_ - pmin_)) + pmin_; break;
            case EXPONENTIAL:
                val_ = ac_->Process();
                val_ = ((val_ * val_) * (pmax_ - pmin_)) + pmin_;
                break;
            case LOGARITHMIC:
                val_ = expf((ac_->Process() * (lmax_ - lmin_)) + lmin_);
                break;
            case CUBE:
                val_ = ac_->Process();
                val_ = ((val_ * (val_ * val_)) * (pmax_ - pmin_)) + pmin_;
                break;
            default: break;
        }
        return val_;
    }

    float Value() const { return val_; }

    bool Moved() const { return false; } // stub; optional

private:
    Curve curve_ = LINEAR;

    float val_;

    float pmin_;
    float pmax_;
    Curve pcurve_;
    AnalogControl* ac_;
    float lmin_;
    float lmax_;
};

class DaisySeed {
  public:
    void StartLog(bool) {}
    float AudioSampleRate() const { return 48000.f; }
    void SetAudioBlockSize(size_t) {}
    // <-- add this so cenote.cpp compiles:
    int GetPin(int /*which*/) const { return 0; } // dummy pin id
    void Print(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
    }
    void PrintLine(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        printf("\n");
        va_end(args);
    }
};

class DaisyPetal {
  public:
    DaisySeed seed;
    std::array<AnalogControl, 6> knob;
    std::array<Switch, 8> switches;
    float AudioSampleRate() const { return seed.AudioSampleRate(); }
    void Init() {}
    void SetAudioBlockSize(size_t) {}
    void ProcessAllControls() {}
};

// 1) AudioHandle buffer typedefs used by your callback signature
struct AudioHandle {
    using InterleavingInputBuffer  = float*;
    using InterleavingOutputBuffer = float*;
};


// ---- Upgraded LED stub (emulator + hardware API parity) ----
class Led {
public:
    void Init(int /*pin*/, bool inverted) { inverted_ = inverted; }
    void Set(float v)
    {
        // clamp 0..1 and apply inversion
        v = (v < 0.f ? 0.f : (v > 1.f ? 1.f : v));
        level_ = inverted_ ? (1.f - v) : v;
        dirty_.store(true, std::memory_order_release);
    }
    void Update() {} // no-op in emulator

    // Emulator helpers:
    float Level() const { return level_; } // 0..1
    bool  ConsumeDirtyFlag() { return dirty_.exchange(false, std::memory_order_acq_rel); }

private:
    float level_ = 0.0f;
    bool  inverted_ = false;
    std::atomic<bool> dirty_{ false };
};


// 3) Minimal System::Delay
namespace System {
inline void Delay(int /*ms*/) {}
inline uint32_t GetNow() { return 0; } // stub; optional
}




} // namespace daisy
