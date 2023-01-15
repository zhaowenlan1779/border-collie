#ifndef _SRGB_GLSL
#define _SRGB_GLSL

float sRGB(float x) {
    if (x <= 0.00031308)
        return 12.92 * x;
    else
        return 1.055 * pow(x, (1.0 / 2.4)) - 0.055;
}

vec3 SRGB_FromLinear(vec3 c) {
    return vec3(sRGB(c.x), sRGB(c.y), sRGB(c.z));
}

#endif
