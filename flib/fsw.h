#pragma once
#ifndef HUGO_TEMPLATE_FSW_H
#define HUGO_TEMPLATE_FSW_H

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

#endif // HUGO_TEMPLATE_FSW_H
