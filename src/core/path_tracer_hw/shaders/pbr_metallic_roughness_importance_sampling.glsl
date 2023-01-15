// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#ifndef _PBR_METALLIC_ROUGHNESS_GLSL
#define _PBR_METALLIC_ROUGHNESS_GLSL

#include "core/path_tracer_hw/shaders/rng.glsl"

#define PI 3.1415926

// Also named D in literature
float GGX_Microfacet(vec3 H, float alpha) {
    const float z = H.z * H.z * (alpha * alpha - 1) + 1;
    return alpha * alpha / (PI * z * z);
}

// G1
float SmithGGXMasking(vec3 wo, float alpha) {
    const float a2 = alpha * alpha;
    return 2 * wo.z / (sqrt(a2 + (1 - a2) * wo.z * wo.z) + wo.z);
}

// G2
float SmithGGXMaskingShadowing(vec3 wi, vec3 wo, float alpha) {
    float a2 = alpha * alpha;
    float denomA = wo.z * sqrt(a2 + (1.0f - a2) * wi.z * wi.z);
    float denomB = wi.z * sqrt(a2 + (1.0f - a2) * wo.z * wo.z);

    return 2 * wi.z * wo.z / (denomA + denomB);
}

vec3 sampleGGXVNDF(vec3 V_, float alpha, float U1, float U2) {
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
    return n;
}

//====================================================================
// https://hal.archives-ouvertes.fr/hal-01509746/document
// vec3 GgxVndf(vec3 N, vec3 wo, float alpha, float u1, float u2) {
//     // Convert wo to tangent space where N is (0, 1, 0): basis (-Nt, N, Nb)
//     vec3 Nt, Nb;
//     CreateCoordinateSystem(N, Nt, Nb);

//     wo = vec3(dot(wo, -Nt), dot(wo, N), dot(wo, Nb));

//     // -- Stretch the view vector so we are sampling as though
//     // -- roughness==1
//     vec3 v = normalize(vec3(wo.x * alpha, wo.y, wo.z * alpha));

//     // -- Build an orthonormal basis with v, t1, and t2
//     vec3 t1 = (v.y < 0.999) ? normalize(cross(v, vec3(0, 1, 0))) : vec3(1, 0, 0);
//     vec3 t2 = cross(t1, v);

//     // -- Choose a point on a disk with each half of the disk weighted
//     // -- proportionally to its projection onto direction v
//     float a = 1.0f / (1.0f + v.y);
//     float r = sqrt(u1);
//     float phi = (u2 < a) ? (u2 / a) * PI : PI + (u2 - a) / (1.0f - a) * PI;
//     float p1 = r * cos(phi);
//     float p2 = r * sin(phi) * ((u2 < a) ? 1.0f : v.y);

//     // -- Calculate the normal in this stretched tangent space
//     vec3 n = p1 * t1 + p2 * t2 + sqrt(max(0, 1 - p1 * p1 - p2 * p2)) * v;

//     // -- unstretch and normalize the normal
//     vec3 result = normalize(vec3(alpha * n.x, max(0, n.y), alpha * n.z));

//     // Convert result back from (-Nt, N, Nb) space
//     return -result.x * Nt + result.y * N + result.z * Nb;
// }

// Out reflectance is before frensel is applied
vec3 ImportanceSampleGgxVdn(vec3 wo, float alpha) {
    float a2 = alpha * alpha;

    float r0 = rnd(prd.seed);
    float r1 = rnd(prd.seed);

    // Sampled half vector
    vec3 wm = sampleGGXVNDF(wo, alpha, r0, r1);

    // Reflect wo with wm
    return 2 * dot(wo, wm) * wm - wo;
}

// Randomly sampling around +Z. PDF is 1 / PI
vec3 ImportanceSampleCosine() {
    float r1 = rnd(prd.seed);
    float r2 = rnd(prd.seed);
    float sq = sqrt(1.0 - r2);
    return vec3(cos(2 * PI * r1) * sq, sin(2 * PI * r1) * sq, sqrt(r2));
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

// Return the tangent and binormal from the incoming normal
void createCoordinateSystem(in vec3 N, out vec3 Nt, out vec3 Nb) {
    if (abs(N.x) > abs(N.y))
        Nt = vec3(N.z, 0, -N.x) / sqrt(N.x * N.x + N.z * N.z);
    else
        Nt = vec3(0, -N.z, N.y) / sqrt(N.y * N.y + N.z * N.z);
    Nb = cross(N, Nt);
}

// Returns microfacet visible normal with GGX distribution
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
    float phi = (Xi.y < a) ? Xi.y / a * PI : PI + (Xi.y - a) / (1.0 - a) * PI;
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

// Note: Output is actually cosine weighted BRDF
void ImportanceSample(vec3 base_color, float metallic, float roughness, vec3 V, vec3 N, out vec3 wi,
                      out vec3 reflectance) {

    vec3 Nt, Nb;
    createCoordinateSystem(N, Nt, Nb);
    V = vec3(dot(V, Nt), dot(V, Nb), dot(V, N));

    const float alpha = roughness * roughness;

    const float p = metallic;
    if (rnd(prd.seed) < p) {
        // Metallic
        wi = sample_ggx_vndf(V, alpha);
        const vec3 H = normalize(wi + V);
        float G2 = G2_Smith(dot(N, wi), dot(N, V), alpha);
        float G1 = G1_Smith(dot(N, V), alpha);
        reflectance = (base_color + (1 - base_color) * pow(1 - abs(dot(V, H)), 5)) * G2 / G1;
    } else {
        // Dielectric
        wi = ImportanceSampleCosine();
        const vec3 H = normalize(wi + V);
        const float f0 = 0.04 + (1 - 0.04) * pow(1 - abs(dot(V, H)), 5);

        const vec3 f_diffuse = (1 / PI) * base_color;
        const vec3 f_specular =
            vec3(GGX_Microfacet(H, alpha) * G2_Smith(dot(N, wi), dot(N, V), alpha) /
                 (4 * dot(N, V) * dot(N, wi)));
        reflectance = mix(f_diffuse, f_specular, f0) / PI;
    }

    // // This is an estimation of (1 - metallic) * (0.04 + 0.96 * (1 - abs(VdotH))^5
    // // Kind of a heuristic, but vk_raytrace also agrees
    // const float p = 0.5 * (1 - metallic);
    // if (rnd(prd.seed) < p) {
    //     wi = ImportanceSampleCosine();
    // } else {
    //     wi = sample_ggx_vndf(V, alpha);
    // }
    // const vec3 H = normalize(wi + V);

    // // See glTF Spec
    // const vec3 c_diff = mix(base_color.rgb, vec3(0), metallic);
    // const vec3 f0 = mix(vec3(0.04), base_color.rgb, metallic);
    // const vec3 F = f0 + (1 - f0) * pow(1 - abs(dot(wi, H)), 5);

    // const float cosine_theta = dot(wi, N);

    // const float factor = GGX_Microfacet(H, alpha);
    // const vec3 f_diffuse = (1 - F) * (1 / PI) * c_diff;
    // const float pdf_cosine = cosine_theta / PI;

    // // The GGX values need to be multipled by the factor to be correct
    // const vec3 f_specular =
    //     F * G2_Smith(dot(N, wi), dot(N, V), alpha) / (4 * dot(N, V) * dot(N, wi));
    // const float pdf_ggx = G1_Smith(dot(N, V), alpha) / (dot(N, V) * 4);
    // if (factor < 0.001) {
    //     reflectance = f_diffuse * cosine_theta / (p * pdf_cosine);
    // } else if (factor > 1000) {
    //     reflectance = f_specular * cosine_theta / ((1 - p) * pdf_ggx);
    // } else {
    //     const float pdf = mix(pdf_ggx * factor, pdf_cosine, p);
    //     reflectance = (f_specular * factor + f_diffuse) * cosine_theta / pdf;
    // }

    // Calculate PDF

    // float G2 = G2_Smith(dot(N, wi), dot(N, V), alpha);

    // // Masking
    // float G1 = G1_Smith(dot(N, V), alpha);

    // // // float G1 = SmithGGXMasking(V, alpha);
    // // // float G2 = SmithGGXMaskingShadowing(wi, V, alpha);
    // reflectance = F * G2 / G1;

    wi = wi.x * Nt + wi.y * Nb + wi.z * N;
}

// #define M_PI 3.141592

// float D_GGX(float NdotH, float alphaRoughness) {
//     float alphaRoughnessSq = alphaRoughness * alphaRoughness;
//     float f = (NdotH * NdotH) * (alphaRoughnessSq - 1.0) + 1.0;
//     return alphaRoughnessSq / (M_PI * f * f);
// }

// vec3 BRDF_specularGGX(vec3 f0, vec3 f90, float alphaRoughness, float VdotH, float NdotL,
//                       float NdotV, float NdotH) {
//     vec3 F = F_Schlick(f0, f90, VdotH);
//     float V = V_GGX(NdotL, NdotV, alphaRoughness);
//     float D = D_GGX(NdotH, max(0.001, alphaRoughness));

//     return F * V * D;
// }

// vec3 EvalSpecularGltf(float roughness, vec3 f0, vec3 f90, vec3 V, vec3 N, vec3 L, vec3 H,
//                       out float pdf) {
//     pdf = 0;
//     float NdotL = dot(N, L);

//     if (NdotL < 0.0)
//         return vec3(0.0);

//     float NdotV = dot(N, V);
//     float NdotH = clamp(dot(N, H), 0, 1);
//     float LdotH = clamp(dot(L, H), 0, 1);
//     float VdotH = clamp(dot(V, H), 0, 1);

//     NdotL = clamp(NdotL, 0.001, 1.0);
//     NdotV = clamp(abs(NdotV), 0.001, 1.0);

//     pdf = D_GGX(NdotH, roughness) * NdotH / (4.0 * LdotH);
//     return BRDF_specularGGX(f0, f90, roughness, VdotH, NdotL, NdotV, NdotH);
// }

// vec3 GgxSampling(float specularAlpha, float r1, float r2) {
//     float phi = r1 * 2.0 * M_PI;

//     float cosTheta = sqrt((1.0 - r2) / (1.0 + (specularAlpha * specularAlpha - 1.0) * r2));
//     float sinTheta = clamp(sqrt(1.0 - (cosTheta * cosTheta)), 0.0, 1.0);
//     float sinPhi = sin(phi);
//     float cosPhi = cos(phi);

//     return vec3(sinTheta * cosPhi, sinTheta * sinPhi, cosTheta);
// }

// vec3 PbrSample(vec3 V, vec3 N, inout vec3 L, inout float pdf) {
//     pdf = 0.0;
//     vec3 brdf = vec3(0.0);
//     float diffuseRatio = 0.5 * (1.0 - state.mat.metallic);
// }

#endif
