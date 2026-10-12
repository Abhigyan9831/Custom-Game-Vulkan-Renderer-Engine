#version 450

layout(set = 0, binding = 0) uniform Ubo {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 lightDir;
    vec4 cameraPos;
};

layout(set = 1, binding = 0) uniform sampler2D baseColorTex;
layout(set = 1, binding = 1) uniform sampler2D metallicRoughnessTex;
layout(set = 1, binding = 2) uniform sampler2D normalTex;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

float distributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

float geometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    return geometrySchlickGGX(max(dot(N, V), 0.0), roughness)
         * geometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

void main()
{
    vec4 baseTex = texture(baseColorTex, fragUV);
    if (baseTex.a < 0.02) {
        discard;
    }

    vec3 albedo = baseTex.rgb * fragColor;

    vec3 mrSample = texture(metallicRoughnessTex, fragUV).rgb;
    float metallic = mrSample.b;
    float roughness = clamp(mrSample.g, 0.05, 1.0);

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(cameraPos.xyz - fragWorldPos);
    vec3 L = normalize(-lightDir.xyz);
    vec3 H = normalize(V + L);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 radiance = vec3(4.2, 4.0, 3.8);

    float NDF = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(clamp(dot(H, V), 0.0, 1.0), F0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    float NdotL = max(dot(N, L), 0.0);
    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;

    vec3 skyColor    = vec3(0.60, 0.65, 0.70);
    vec3 groundColor = vec3(0.40, 0.38, 0.35);
    float hemi = 0.5 + 0.5 * N.y;
    vec3 ambient = mix(groundColor, skyColor, hemi) * albedo;

    vec3 color = ambient + Lo;

    // Clean Reinhard tone mapping to preserve rich pink/rose petal saturation
    color = color / (color + vec3(1.0));

    outColor = vec4(color, 1.0);
}