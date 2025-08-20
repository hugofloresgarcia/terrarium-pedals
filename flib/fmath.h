#pragma once

#ifndef HUGO_LIB_FMATH_H
#define HUGO_LIB_FMATH_H

inline float randf(float min, float max) {
    return min + (max - min) * (static_cast<float>(rand()) / RAND_MAX);
}

inline float linlin(float x, float a, float b, float c, float d) {
if (x <= a)
    return c;
if (x >= b)
    return d;
return (x - a) / (b - a) * (d - c) + c;
}

inline float eq_power_xfade(float a, float b, float t) {
    float theta = t * M_PI_2;
    float wa = cos(theta);
    float wb = sin(theta);
    return a * wa + b * wb;
}

inline float linear_xfade(float a, float b, float t) {
    return a * (1 - t) + b * t;
}

#endif // HUGO_LIB_FMATH_H