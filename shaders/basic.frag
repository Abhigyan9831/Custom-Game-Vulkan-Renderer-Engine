#version 450

layout(set = 0, binding = 0) uniform Ubo {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 lightDir;
    vec4 cameraPos;
    vec4 sunColor;
    vec4 pointLights[8];
    vec4 pointLightCount;
    mat4 lightSpaceMatrix;
};

layout(set = 1, binding = 0) uniform sampler2D baseColorTex;
layout(set = 1, binding = 1) uniform sampler2D metallicRoughnessTex;
layout(set = 1, binding = 2) uniform sampler2D normalTex;
layout(set = 1, binding = 3) uniform sampler2DShadow shadowMap;

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
    if (!gl_FrontFacing) N = -N;
    vec3 V = normalize(cameraPos.xyz - fragWorldPos);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float shadow = 0.0;
    {
        vec4 lightSpacePos = lightSpaceMatrix * vec4(fragWorldPos, 1.0);
        vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
        projCoords = projCoords * 0.5 + 0.5;
        if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
            projCoords.y < 0.0 || projCoords.y > 1.0 || projCoords.z > 1.0) {
            shadow = 0.0;
        } else {
            float NdotL_sun = max(dot(N, normalize(-lightDir.xyz)), 0.0);
            float bias = max(0.0015 * (1.0 - NdotL_sun), 0.0005);
            shadow = 1.0 - texture(shadowMap, vec3(projCoords.xy, projCoords.z - bias));
        }
    }

    vec3 Lo = vec3(0.0);

    // ---- Sun (your values, now data-driven) ----
    {
        vec3 L = normalize(-lightDir.xyz);
        vec3 H = normalize(V + L);
        vec3 radiance = sunColor.rgb;

        float NDF = distributionGGX(N, H, roughness);
        float G = geometrySmith(N, V, L, roughness);
        vec3 F = fresnelSchlick(clamp(dot(H, V), 0.0, 1.0), F0);

        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3 specular = numerator / denominator;

        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo / PI + specular) * radiance * NdotL * (1.0 - shadow);
    }

    // ---- Point lights (same Cook-Torrance, per-light L + attenuation) ----
    int count = int(pointLightCount.x);
    for (int i = 0; i < 4 && i < count; ++i) {
        vec3 toLight = pointLights[i * 2].xyz - fragWorldPos;
        float dist = length(toLight);
        float radius = pointLights[i * 2].w;

        vec3 L = toLight / max(dist, 0.0001);
        vec3 H = normalize(V + L);

        float window = clamp(1.0 - (dist * dist) / (radius * radius), 0.0, 1.0);
        float attenuation = (window * window) / max(dist * dist, 0.0001);
        vec3 radiance = pointLights[i * 2 + 1].rgb * attenuation;

        float NDF = distributionGGX(N, H, roughness);
        float G = geometrySmith(N, V, L, roughness);
        vec3 F = fresnelSchlick(clamp(dot(H, V), 0.0, 1.0), F0);

        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3 specular = numerator / denominator;

        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }

    vec3 skyColor    = vec3(0.75, 0.75, 0.75);
    vec3 groundColor = vec3(0.55, 0.55, 0.55);
    float hemi = 0.5 + 0.5 * N.y;
    vec3 ambient = mix(groundColor, skyColor, hemi) * albedo;

    
    vec3 ambientSpec = mix(groundColor, skyColor, hemi)
                     * fresnelSchlick(max(dot(N, V), 0.0), F0) * 0.25;

    vec3 color = ambient + ambientSpec + Lo;

    
    color = color / (color + vec3(1.0));

    outColor = vec4(color, 1.0);
}