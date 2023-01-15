// Copyright 2023 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// Note: See Appendix B.3 of the glTF Specification
#define PI 3.1415926

float GGX_Microfacet(float alpha, vec3 N, vec3 H) {
    if (dot(N, H) <= 0) {
        return 0;
    }
    const float z = dot(N, H) * dot(N, H) * (alpha * alpha - 1) + 1;
    return alpha * alpha / (PI * z * z);
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

vec3 BRDF(vec3 base_color, float metallic, float roughness, vec3 V, vec3 L, vec3 N, vec3 H) {
    const vec3 c_diff = mix(base_color.rgb, vec3(0), metallic);
    const vec3 f0 = mix(vec3(0.04), base_color.rgb, metallic);
    const float alpha = roughness * roughness;
    const vec3 F = f0 + (1 - f0) * pow(1 - abs(dot(V, H)), 5);

    const vec3 f_diffuse = (1 - F) * (1 / PI) * c_diff;
    const vec3 f_specular = F * GGX_Microfacet(alpha, N, H) * Visibility(alpha, V, L, N, H);
    return f_diffuse + f_specular;
}
