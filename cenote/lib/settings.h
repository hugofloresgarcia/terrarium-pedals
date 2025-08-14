// settings_menu.h
#pragma once

#include "daisy_petal.h"

using namespace daisy;
using namespace terrarium;

class SettingsMenu {
public:
    void Init(DaisyPetal* hw, Led* led1, Led* led2) {
        this->hw = hw;
        this->led1 = led1;
        this->led2 = led2;
        in_menu = false;
        fsw1_tap_count = 0;
        last_sw_state = 0;
        blink_timer = 0;
        led1_on = false;
        changed_flags = 0;
        System::Delay(10);
    }

    void Process(bool fsw1_rising, bool fsw2_rising, bool fsw1_pressed, bool fsw2_pressed) {
        // Enter menu detection
        if(!in_menu && fsw1_pressed && fsw2_pressed) {
            if(fsw1_rising) {
                fsw1_tap_count++;
                if(fsw1_tap_count >= 4) {
                    EnterMenu();
                }
            }
        } else if (!fsw1_pressed || !fsw2_pressed) {
            fsw1_tap_count = 0;
        }

        // In menu
        if(in_menu) {
            BlinkLed1();
            for(int i = 0; i < 4; i++) {
                bool sw_now = hw->switches[i + 2].Pressed(); // SW_1 to SW_4
                if(((last_sw_state >> i) & 0x01) != sw_now) {
                    last_sw_state = (last_sw_state & ~(1 << i)) | (sw_now << i);
                    changed_flags |= (1 << i);
                    BlinkLed2(5);
                }
            }

            if(fsw1_rising && fsw2_rising) {
                ExitMenu();
            }
        }

        if(led2_blinking) {
            BlinkLed2Step();
        }
    }

    bool InMenu() const { return in_menu; }

    bool GetSettingUseWetDry() const {
        return (last_sw_state >> 3) & 0x01;
    }

private:
    DaisyPetal* hw;
    Led* led1;
    Led* led2;

    bool in_menu;
    int fsw1_tap_count;

    uint8_t last_sw_state;
    uint8_t changed_flags;

    // LED blinking
    int blink_timer;
    bool led1_on;

    // LED2 blinking
    int led2_blink_counter = 0;
    int led2_blink_total = 0;
    bool led2_blinking = false;
    bool led2_on = false;
    int led2_timer = 0;

    void EnterMenu() {
        in_menu = true;
        fsw1_tap_count = 0;
        last_sw_state = 0;
        for(int i = 0; i < 4; i++) {
            if(hw->switches[i + 2].Pressed()) {
                last_sw_state |= (1 << i);
            }
        }
    }

    void ExitMenu() {
        in_menu = false;
        fsw1_tap_count = 0;
    }

    void BlinkLed1() {
        blink_timer++;
        if(blink_timer >= 50) { // toggle every ~50 * 1ms = 50ms
            led1_on = !led1_on;
            led1->Set(led1_on ? 1.0f : 0.0f);
            led1->Update();
            blink_timer = 0;
        }
    }

    void BlinkLed2(int times) {
        led2_blinking = true;
        led2_blink_counter = 0;
        led2_blink_total = times * 2;
        led2_timer = 0;
        led2_on = false;
    }

    void BlinkLed2Step() {
        led2_timer++;
        if(led2_timer >= 30) { // toggle every ~30ms
            led2_on = !led2_on;
            led2->Set(led2_on ? 1.0f : 0.0f);
            led2->Update();
            led2_timer = 0;
            led2_blink_counter++;
            if(led2_blink_counter >= led2_blink_total) {
                led2_blinking = false;
                led2->Set(0.0f);
                led2->Update();
            }
        }
    }
};
