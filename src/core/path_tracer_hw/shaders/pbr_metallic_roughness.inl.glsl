// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#ifndef _PBR_METALLIC_ROUGHNESS_GLSL
#define _PBR_METALLIC_ROUGHNESS_GLSL

#include "core/path_tracer_hw/shaders/rng.glsl"

#define PI 3.1415926

// Also named D in literature
float GGX_Microfacet(float alpha, vec3 N, vec3 H) {
    if (dot(N, H) <= 0) {
        return 0;
    }
    const float z = dot(N, H) * dot(N, H) * (alpha * alpha - 1) + 1;
    return alpha * alpha / (PI * z * z);
}

// G1
float SmithGGXMasking(vec3 N, vec3 wo, float alpha) {
    const float a2 = alpha * alpha;
    return 2 * dot(N, wo) / (sqrt(a2 + (1 - a2) * dot(N, wo) * dot(N, wo)) + dot(N, wo));
}

// G2
float SmithGGXMaskingShadowing(vec3 N, vec3 wi, vec3 wo, float alpha) {
    float a2 = alpha * alpha;
    float denomA = dot(N, wo) * sqrt(a2 + (1.0f - a2) * dot(N, wi) * dot(N, wi));
    float denomB = dot(N, wi) * sqrt(a2 + (1.0f - a2) * dot(N, wo) * dot(N, wo));

    return 2 * dot(N, wi) * dot(N, wo) / (denomA + denomB);
}

// Return the tangent and binormal from the incoming normal
void CreateCoordinateSystem(in vec3 N, out vec3 Nt, out vec3 Nb) {
    if (abs(N.x) > abs(N.y))
        Nt = vec3(N.z, 0, -N.x) / sqrt(N.x * N.x + N.z * N.z);
    else
        Nt = vec3(0, -N.z, N.y) / sqrt(N.y * N.y + N.z * N.z);
    Nb = cross(N, Nt);
}

vec3 sampleGGXVNDF(vec3 N, vec3 V_, float alpha, float U1, float U2) {
    // Convert wo to tangent space where N is (0, 0, 1): basis (Nt, Nb, N)
    vec3 Nt, Nb;
    CreateCoordinateSystem(N, Nt, Nb);

    V_ = vec3(dot(V_, Nt), dot(V_, Nb), dot(V_, N));

    // stretch view
    vec3 V = normalize(vec3(alpha * V_.x, alpha * V_.y, V_.z));

    // orthonormal basis
    vec3 T1 = (V.z < 0.9999) ? normalize(cross(V, vec3(0, 0, 1))) : vec3(1, 0, 0);
    vec3 T2 = cross(T1, V);

    // sample point with polar coordinates (r, phi)
    float a = 1.0 / (1.0 + V.z);
    float r = sqrt(U1);
    float phi = (U2 < a) ? U2 / a * PI : PI + (U2 - a) / (1.0 - a) * PI;
    float P1 = r * cos(phi);
    float P2 = r * sin(phi) * ((U2 < a) ? 1.0 : V.z);
    // compute normal
    vec3 n = P1 * T1 + P2 * T2 + sqrt(max(0.0, 1.0 - P1 * P1 - P2 * P2)) * V;

    // unstretch
    n = normalize(vec3(alpha * n.x, alpha * n.y, max(0.0, n.z)));
    return n.x * Nt + n.y * Nb + n.z * N;
}

//====================================================================
// https://hal.archives-ouvertes.fr/hal-01509746/document
vec3 GgxVndf(vec3 N, vec3 wo, float alpha, float u1, float u2) {
    // Convert wo to tangent space where N is (0, 1, 0): basis (-Nt, N, Nb)
    vec3 Nt, Nb;
    CreateCoordinateSystem(N, Nt, Nb);

    wo = vec3(dot(wo, -Nt), dot(wo, N), dot(wo, Nb));

    // -- Stretch the view vector so we are sampling as though
    // -- roughness==1
    vec3 v = normalize(vec3(wo.x * alpha, wo.y, wo.z * alpha));

    // -- Build an orthonormal basis with v, t1, and t2
    vec3 t1 = (v.y < 0.999) ? normalize(cross(v, vec3(0, 1, 0))) : vec3(1, 0, 0);
    vec3 t2 = cross(t1, v);

    // -- Choose a point on a disk with each half of the disk weighted
    // -- proportionally to its projection onto direction v
    float a = 1.0f / (1.0f + v.y);
    float r = sqrt(u1);
    float phi = (u2 < a) ? (u2 / a) * PI : PI + (u2 - a) / (1.0f - a) * PI;
    float p1 = r * cos(phi);
    float p2 = r * sin(phi) * ((u2 < a) ? 1.0f : v.y);

    // -- Calculate the normal in this stretched tangent space
    vec3 n = p1 * t1 + p2 * t2 + sqrt(max(0, 1 - p1 * p1 - p2 * p2)) * v;

    // -- unstretch and normalize the normal
    vec3 result = normalize(vec3(alpha * n.x, max(0, n.y), alpha * n.z));

    // Convert result back from (-Nt, N, Nb) space
    return -result.x * Nt + result.y * N + result.z * Nb;
}

// Out reflectance is before frensel is applied
vec3 ImportanceSampleGgxVdn(vec3 N, vec3 wo, float alpha) {
    float a2 = alpha * alpha;

    float r0 = rnd(prd.seed);
    float r1 = rnd(prd.seed);

    // Sampled half vector
    vec3 wm = sampleGGXVNDF(N, wo, alpha, r0, r1);

    // Reflect wo with wm
    return 2 * abs(dot(wo, wm)) * wm - wo;
}

// Randomly sampling around +Z. PDF is 1 / PI
vec3 ImportanceSampleCosine(vec3 N) {
    vec3 Nt, Nb;
    CreateCoordinateSystem(N, Nt, Nb);

    float r1 = rnd(prd.seed);
    float r2 = rnd(prd.seed);
    float sq = sqrt(1.0 - r2);

    vec3 direction = vec3(cos(2 * PI * r1) * sq, sin(2 * PI * r1) * sq, sqrt(r2));
    return direction.x * Nt + direction.y * Nb + direction.z * N;
}

float Visibility(float alpha, vec3 V, vec3 L, vec3 N, vec3 H) {
    if (dot(H, L) <= 0 || dot(H, V) <= 0) {
        return 0;
    }
    const float a =
        abs(dot(N, L)) + sqrt(alpha * alpha + (1 - alpha * alpha) * dot(N, L) * dot(N, L));
    const float b =
        abs(dot(N, V)) + sqrt(alpha * alpha + (1 - alpha * alpha) * dot(N, V) * dot(N, V));
    return 1 / (a * b);
}

// Note: Output is actually cosine weighted BRDF
void ImportanceSample(vec3 base_color, float metallic, float roughness, vec3 V, vec3 N, out vec3 wi,
                      out vec3 reflectance) {

    const float alpha = roughness * roughness;

    // This is an estimation of (1 - metallic) * (0.04 + 0.96 * (1 - abs(VdotH))^5
    // Kind of a heuristic, but vk_raytrace also agrees
    const float p = 0; // 0.5 * (1 - metallic);
    if (rnd(prd.seed) < p) {
        wi = ImportanceSampleCosine(N);
    } else {
        wi = ImportanceSampleGgxVdn(N, V, alpha);
    }
    const vec3 H = normalize(wi + V);

    // See glTF Spec
    const vec3 c_diff = mix(base_color.rgb, vec3(0), metallic);
    const vec3 f0 = mix(vec3(0.04), base_color.rgb, metallic);
    const vec3 F = f0 + (1 - f0) * pow(1 - abs(dot(V, H)), 5);
    // const vec3 f_diffuse = (1 - F) * (1 / PI) * c_diff;
    // const vec3 f_specular = F * GGX_Microfacet(alpha, N, H) *
    //                         SmithGGXMaskingShadowing(N, wi, V, alpha) /
    //                         (4 * dot(N, V) * dot(N, wi));
    // const vec3 f_diffuse = vec3(0);
    // const vec3 f_specular = F *

    // Calculate PDF
    // const float cosine_theta = dot(wi, N);
    // const float pdf_cosine = cosine_theta / PI;
    // const float pdf_ggx =
    //     SmithGGXMasking(N, V, alpha) * GGX_Microfacet(alpha, N, H) / (abs(dot(V, N)) * 4);
    // const float pdf = mix(pdf_ggx, pdf_cosine, p);

    // reflectance = (f_diffuse + f_specular) * cosine_theta / pdf;
    if (dot(N, wi) > 0) {
        float G1 = SmithGGXMasking(N, V, alpha);
        float G2 = SmithGGXMaskingShadowing(N, wi, V, alpha);
        reflectance = F * G2 / G1;
    } else {
        reflectance = vec3(0);
    }
}

#endif
