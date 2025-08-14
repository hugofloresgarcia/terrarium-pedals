#pragma once
#include <array>
#include <cmath>

class ShiftKnobManager
{
public:
    static constexpr float TOUCH_THRESHOLD = 0.005f; // 0.1%

    ShiftKnobManager() : shift_active_(false), num_knobs_(0) {}

    void Init(size_t num_knobs)
    {
        num_knobs_ = num_knobs;
        values_norm_.fill(0.0f);
        values_shift_.fill(0.0f);
        last_hw_values_.fill(0.0f);
        takeover_ready_.fill(true); // ready by default
    }

    void SetShift(bool shift)
    {
        if (shift != shift_active_)
        {
            shift_active_ = shift;
            // On mode change, takeover must be re-caught for all knobs
            takeover_ready_.fill(false);
        }
    }

    bool ProcessKnobs(std::array<float, 8> &hw_knobs)
    {
        bool notify_takeover = false;
        for (size_t i = 0; i < num_knobs_; i++)
        {
            float hw_val = hw_knobs[i];
            float &active_val = shift_active_ ? values_shift_[i] : values_norm_[i];

            if (!takeover_ready_[i])
            {
                // Wait until hardware knob is close enough to stored value
                if (std::fabs(hw_val - active_val) <= TOUCH_THRESHOLD)
                {
                    takeover_ready_[i] = true;
                    notify_takeover = true; // Notify that a takeover has occurred
                }
            }
            else
            {
                // Normal operation
                active_val = hw_val;
            }

            last_hw_values_[i] = hw_val;
        }
        return notify_takeover;
    }

    float GetValue(size_t idx) const
    {
        return shift_active_ ? values_shift_[idx] : values_norm_[idx];
    }

    float GetNormalValue(size_t idx) const { return values_norm_[idx]; }
    float GetShiftValue(size_t idx) const { return values_shift_[idx]; }

private:
    bool shift_active_;
    size_t num_knobs_;
    std::array<float, 8> values_norm_;
    std::array<float, 8> values_shift_;
    std::array<float, 8> last_hw_values_;
    std::array<bool, 8> takeover_ready_;
};
