

inline float linlin(float x, float a, float b, float c, float d) {
if (x <= a)
    return c;
if (x >= b)
    return d;
return (x - a) / (b - a) * (d - c) + c;
}