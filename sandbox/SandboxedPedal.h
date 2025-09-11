#include "../cenote/cenote.cpp"
// #include "../glitch/glitch.cpp"

// When building for the JUCE sandbox, we expose setup/control/audio instead of main().
namespace sandbox {

// mirrors your hardware main(), minus StartAudio/while(1)
void setup()
{
    init();
}

// called by JUCE timer thread at "control rate"
inline void control() { controlBlock(); }

// single-sample adapter: we present one sample to your libDaisy-style callback.
// Your callback expects interleaved mono with i += 2, so we pass size=2 and
// fill the first lane.
inline void audio(float* in, float* out, size_t n)
{
    (void)n; // single-sample path; n is ignored intentionally.

    // tiny interleaved block: [L0, (unused)]
    float in_buf[2]  = { *in, 0.0f };
    float out_buf[2] = { 0.0f, 0.0f };

    callback(in_buf, out_buf, /*size=*/2);
    *out = out_buf[0];
}

} // namespace sandbox