#include <iostream>
#include <vector>
#include <cmath>
#include "wigglr.h"  // your files

int main() {
    constexpr size_t SR = 48000;
    constexpr size_t FRAMES = SR * 5; // 5 seconds
    constexpr size_t CHANS = 2;

    // Allocate buffers
    std::vector<float> in(FRAMES * CHANS, 0.f);
    std::vector<float> out(FRAMES * CHANS, 0.f);

    // Example input: sine wave
    for (size_t i = 0; i < FRAMES; ++i) {
        float t = i / (float)SR;
        float s = std::sin(2.0f * M_PI * 440.f * t);
        in[i * CHANS + 0] = s;
        in[i * CHANS + 1] = s;
    }

    Wigglr wig;
    wig.Init(out.data(), FRAMES, CHANS, SR);

    for (size_t i = 0; i < FRAMES; ++i) {
        wig.ProcessFrame(&in[i * CHANS], &out[i * CHANS]);
    }

    // Print first 10 output samples
    for (size_t i = 0; i < 10; ++i) {
        std::cout << out[i * CHANS] << ", " << out[i * CHANS + 1] << "\n";
    }

    return 0;
}
