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

#endif // HUGO_LIB_FMATH_H