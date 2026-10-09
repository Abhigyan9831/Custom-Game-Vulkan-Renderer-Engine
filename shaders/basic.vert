#version 450

layout(set = 0, binding = 0) uniform Ubo {
    mat4 model;
    mat4 view;
    mat4 proj;
};

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main()
{
    gl_Position = proj * view * model * vec4(inPos, 1.0);
    fragColor = inColor;
}