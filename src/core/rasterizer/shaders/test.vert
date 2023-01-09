#version 450
#extension GL_GOOGLE_include_directive : enable

#include "core/shaders/renderer_glsl.h"

layout(binding = 0) uniform RasterizerUBOBlock {
    RasterizerUBO u;
}
uni;

layout(push_constant) uniform constants {
    mat4 transformation;
}
push_constants;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main() {
    gl_Position = push_constants.transformation * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
