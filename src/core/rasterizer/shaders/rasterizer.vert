#version 460

layout(push_constant) uniform constants {
    mat4 transformation;
}
push_constants;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inTexCoord0;
layout(location = 3, component = 2) in vec2 inTexCoord1;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord0;
layout(location = 2, component = 2) out vec2 fragTexCoord1;

void main() {
    gl_Position = push_constants.transformation * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragNormal = inNormal;
    fragTexCoord0 = inTexCoord0;
    fragTexCoord1 = inTexCoord1;
}
