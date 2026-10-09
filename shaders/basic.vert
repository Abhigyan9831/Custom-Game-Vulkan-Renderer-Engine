
#version 450

layout(set = 0, binding = 0) uniform Ubo {
    mat4 model;
};

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main()
{
    gl_Position = model * vec4(inPos, 0.0, 1.0);
    fragColor = inColor;
}