#version 450

layout(set = 0, binding = 0) uniform Ubo {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 lightDir;
    vec4 cameraPos;
};

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inUV;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragUV;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragWorldPos;

void main()
{
    vec4 worldPos = model * vec4(inPos, 1.0);
    gl_Position = proj * view * worldPos;
    fragColor = inColor;
    fragUV = inUV;
    fragNormal = mat3(model) * inNormal;
    fragWorldPos = worldPos.xyz;
}