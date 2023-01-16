// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#ifndef _PBR_METALLIC_ROUGHNESS_GLSL
#define _PBR_METALLIC_ROUGHNESS_GLSL

#include "core/path_tracer_hw/shaders/rng.glsl"

#define M_PI 3.1415926

// Randomly sampling around +Z. PDF is 1 / PI
vec3 ImportanceSampleCosine() {
    float r1 = rnd(prd.seed);
    float r2 = rnd(prd.seed);
    float sq = sqrt(1.0 - r2);
    return vec3(cos(2 * M_PI * r1) * sq, sin(2 * M_PI * r1) * sq, sqrt(r2));
}

// Return the tangent and binormal from the incoming normal
void createCoordinateSystem(in vec3 N, out vec3 Nt, out vec3 Nb) {
    if (abs(N.x) > abs(N.y))
        Nt = vec3(N.z, 0, -N.x) / sqrt(N.x * N.x + N.z * N.z);
    else
        Nt = vec3(0, -N.z, N.y) / sqrt(N.y * N.y + N.z * N.z);
    Nb = cross(N, Nt);
}

// Returns microfacet visible normal with GGX distribution
// Adapted from https://www.shadertoy.com/view/NscBWs, method is from paper
// https://hal.science/hal-01509746/document
vec3 sample_ggx_vndf(vec3 V_tangent, float alpha) {
    vec2 Xi = vec2(rnd(prd.seed), rnd(prd.seed));

    // Stretch view -- isotropic
    vec3 V = normalize(vec3(alpha * V_tangent.xy, V_tangent.z));

    // Section 4.1: orthonormal basis (with special case if cross product is zero)
    float lensq = V.x * V.x + V.y * V.y;
    vec3 T1 = lensq > 0.0 ? vec3(-V.y, V.x, 0.0) * inversesqrt(lensq) : vec3(1.0, 0.0, 0.0);
    vec3 T2 = cross(V, T1);

    // Sample point with polar coordinates (r, phi)
    float a = 1.0 / (1.0 + V.z);
    float r = sqrt(Xi.x);
    float phi = (Xi.y < a) ? Xi.y / a * M_PI : M_PI + (Xi.y - a) / (1.0 - a) * M_PI;
    float P1 = r * cos(phi);
    float P2 = r * sin(phi) * ((Xi.y < a) ? 1.0 : V.z);

    // Compute normal
    vec3 H = P1 * T1 + P2 * T2 + sqrt(max(0.0, 1.0 - P1 * P1 - P2 * P2)) * V;

    // Unstretch
    H = vec3(alpha * H.xy, max(0.0, H.z));

    // Normalize and output
    return normalize(H);
}

// Lambda used in G2-G1 functions
float Lambda_Smith(float NdotX, float alpha) {
    float alpha_sqr = alpha * alpha;
    float NdotX_sqr = NdotX * NdotX;
    return (-1.0 + sqrt(alpha_sqr * (1.0 - NdotX_sqr) / NdotX_sqr + 1.0)) * 0.5;
}

// Masking function
float G1_Smith(float NdotV, float alpha) {
    float lambdaV = Lambda_Smith(NdotV, alpha);

    return 1.0 / (1.0 + lambdaV);
}

// Height Correlated Masking-shadowing function
float G2_Smith(float NdotL, float NdotV, float alpha) {
    float lambdaV = Lambda_Smith(NdotV, alpha);
    float lambdaL = Lambda_Smith(NdotL, alpha);

    return 1.0 / (1.0 + lambdaV + lambdaL);
}

void ImportanceSamplePerfectSpecular(vec3 base_color, float metallic, vec3 V, vec3 N, out vec3 wi,
                                     out vec3 reflectance) {
    const vec3 f0 = mix(vec3(0.04), base_color.rgb, metallic);
    const vec3 F = f0 + (1 - f0) * pow(1 - abs(dot(V, N)), 5); // H = N
    wi = reflect(-V, N);
    reflectance = F;
}

// Output is cosine weighted & pdf-adjusted
void ImportanceSample(vec3 base_color, float metallic, float roughness, vec3 V, vec3 N, out vec3 wi,
                      out vec3 reflectance) {
    // if (dot(N, V) < 0) {
    //     N = -N;
    // }
    if (metallic > 0.999 && roughness < 0.001) {
        // Special case: BRDF is almost infinite
        ImportanceSamplePerfectSpecular(base_color, metallic, V, N, wi, reflectance);
        return;
    }

    // Change of basis such that N is +Z
    vec3 Nt, Nb;
    createCoordinateSystem(N, Nt, Nb);
    V = vec3(dot(V, Nt), dot(V, Nb), dot(V, N));

    const float alpha = roughness * roughness;
    const vec3 c_diff = mix(base_color.rgb, vec3(0), metallic);
    const vec3 f0 = mix(vec3(0.04), base_color.rgb, metallic);

    // This is an estimation of (1 - metallic) * (0.04 + 0.96 * (1 - abs(VdotH))^5
    // Kind of a heuristic, but vk_raytrace also agrees
    const float p = 0.5 * (1 - metallic);
    if (rnd(prd.seed) < p) { // Estimate diffuse
        wi = ImportanceSampleCosine();
        const vec3 H = normalize(wi + V);
        const vec3 F = f0 + (1 - f0) * pow(1 - abs(dot(V, H)), 5);
        // BRDF is c_diff / PI, PDF is cosine / PI.
        reflectance = (1 - F) * c_diff / p;
    } else { // Estimate specular
        wi = sample_ggx_vndf(V, alpha);
        const vec3 H = normalize(wi + V);
        const vec3 F = f0 + (1 - f0) * pow(1 - abs(dot(V, H)), 5);
        float G2 = G2_Smith(wi.z, V.z, alpha);
        float G1 = G1_Smith(V.z, alpha);
        reflectance = F * G2 / G1 / (1 - p);
    }

    wi = wi.x * Nt + wi.y * Nb + wi.z * N;
}

#endif
