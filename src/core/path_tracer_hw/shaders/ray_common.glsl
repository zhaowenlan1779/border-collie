struct hitPayload {
    vec3 hit_value;
    uint seed;
    uint depth;
    // Next ray
    vec3 ray_origin;
    vec3 ray_direction;
    vec3 weight;
};
