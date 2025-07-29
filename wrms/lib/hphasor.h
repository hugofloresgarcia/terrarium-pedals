#pragma once

#ifndef FE_HPHASOR_H
#define FE_HPHASOR_H
#ifdef __cplusplus

#include <math.h>
#include "daisysp.h"

namespace daisysp
{
/** Generates a normalized signal moving from 0-1 at the specified frequency.

\todo Selecting which channels should be initialized/included in the sequence conversion.
\todo Setup a similar start function for an external mux, but that seems outside the scope of this file.

*/
class HPhasor
{
  public:
    HPhasor() {}
    ~HPhasor() {}
    /** Initializes the HPhasor module
    sample rate, and freq are in Hz
    initial phase is in radians
    Additional Init functions have defaults when arg is not specified:
    - phs = 0.0f
    - freq = 1.0f
    */
    inline void Init(float sample_rate, float freq = 1.0f, float initial_phase = 0.0f)
    {
        sample_rate_ = sample_rate;
        phs_         = initial_phase;
        SetFreq(freq);
    }

    /** processes HPhasor and returns current value
    */
    float Process(uint8_t *on_cycle_end = nullptr) {
        float out;
        out = phs_ / TWOPI_F;
        phs_ += inc_;
        if(phs_ > TWOPI_F)
        {
            if (on_cycle_end) *on_cycle_end = 1; // signal has completed a cycle
            phs_ -= TWOPI_F;
        }
        if(phs_ < 0.0f)
        {
            phs_ = 0.0f;
        }
        return out;
    }


    /** Sets frequency of the HPhasor in Hz
    */
    void SetFreq(float freq)
    {
      freq_ = freq;
      inc_  = (TWOPI_F * freq_) / sample_rate_;
    }

    /** Set the phase in range [0, 1]
    * @param pos phase in range [0, 1]
    */
    void SetPhase(float pos)
    {
        pos = fclamp(pos, 0.f, 1.f);
        phs_ = pos * TWOPI_F; // convert to radians
    }

    /** Returns current frequency value in Hz
    */
    inline float GetFreq() { return freq_; }

  private:
    float freq_;
    float sample_rate_, inc_, phs_;
};
} // namespace daisysp
#endif
#endif
