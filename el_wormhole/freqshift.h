#pragma once
#include <cmath>
#include <array>

namespace daisysp
{

/**
   @brief 12-pole (6 per side) IIR Hilbert-based single-sideband frequency shifter,
          ported and simplified from SuperCollider’s FreqShift UGen.
*/
class FrequencyShifter
{
  public:
    FrequencyShifter() {}
    ~FrequencyShifter() {}

    /// Initialize filter poles and reset phase
    void Init(float sample_rate)
    {
        sample_rate_ = sample_rate;
        phase_       = 0.f;
        freqShiftHz_ = 0.f;

        // compute each all-pass coefficient from its center frequency
        // (from SC: 15 * π / fs times a set of 12 multipliers)
        const double gamconst = (15.0 * M_PI) / sample_rate_;
        constexpr double gamMul[12] = {
            0.3609,  2.7412, 11.1573, 44.7581,
           179.6242,798.4578, 1.2524,  5.5671,
            22.3423,89.6271,364.7914,2770.1114
        };
        for(int i = 0; i < 12; i++)
        {
            double g    = gamconst * gamMul[i];
            coefs_[i]   = float((g - 1.0) / (g + 1.0));
            y1_[i]      = 0.f;
        }
    }

    /// Set the desired frequency shift in Hz
    void SetShift(float hz)
    {
        freqShiftHz_ = hz;
    }

    /// Process a single sample through Hilbert IIR + SSB modulation
    float Process(float in)
    {
        // 1) Run 6 all-pass stages for the I branch
        float I = runHilbert(in, 0);
        // 2) And 6 more for the Q branch (taps 6–11)
        float Q = runHilbert(in, 6);

        // 3) Advance and wrap the phase
        phase_ += (2.f * float(M_PI) * freqShiftHz_) / sample_rate_;
        if(phase_ >=  2.f * float(M_PI)) phase_ -= 2.f * float(M_PI);
        if(phase_ <   0.f)              phase_ += 2.f * float(M_PI);

        // 4) Compute sin/cos
        float s = std::sin(phase_);
        float c = std::cos(phase_);

        // 5) SSB out = I·cos(φ) + Q·sin(φ)
        return I * c + Q * s;
    }

  private:
    /// Run six 1-pole all-pass stages starting at offset `ofs`
    inline float runHilbert(float x, int ofs)
    {
        double v = x;
        for(int i = 0; i < 6; i++)
        {
            double y0 = v - coefs_[ofs+i] * y1_[ofs+i];
            double ay = coefs_[ofs+i] * y0 + y1_[ofs+i];
            y1_[ofs+i] = float(y0);
            v = ay;
        }
        return float(v);
    }

    float                 sample_rate_;
    float                 phase_, freqShiftHz_;
    std::array<float,12>  coefs_, y1_;
};

} // namespace daisysp
