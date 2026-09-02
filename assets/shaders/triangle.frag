#version 450

const float PI = 3.14159265359;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec4 inShadowCoord0;
layout(location = 4) in vec4 inShadowCoord1;
layout(location = 5) in vec4 inShadowCoord2;
layout(location = 6) in vec3 inTangent;
layout(location = 7) in vec3 inBitangent;
layout(location = 8) in vec4 inShadowCoordSpot[3];

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 lightSpaceMatrices[3];
    vec4 cascadeSplits; // w = environment exposure (IBL), unused slot repurposed
    vec4 lightDirection;
    vec4 cameraPosition;
} camera;

layout(set = 0, binding = 1) uniform sampler2DShadow shadowMap0;
layout(set = 0, binding = 2) uniform sampler2DShadow shadowMap1;
layout(set = 0, binding = 3) uniform sampler2DShadow shadowMap2;
layout(set = 0, binding = 4) uniform sampler2DShadow shadowMapSpot[3];

// IBL (Phase 6D): raw environment (skybox, unused here), diffuse irradiance,
// specular prefiltered mip chain, split-sum BRDF LUT. All at raw HDR scale —
// camera.cascadeSplits.w (environment exposure) is applied to the ambient
// term below so IBL-lit surfaces match the skybox's exposure.
layout(set = 0, binding = 6) uniform samplerCube environmentMap;
layout(set = 0, binding = 7) uniform samplerCube irradianceMap;
layout(set = 0, binding = 8) uniform samplerCube prefilteredEnvMap;
layout(set = 0, binding = 9) uniform sampler2D brdfLUT;
// Keep in sync with VulkanRHI.h's PREFILTER_MIP_COUNT - 1.
const float MAX_REFLECTION_LOD = 4.0;

// Must stay in sync with MAX_SPOT_LIGHTS / MAX_SPOT_SHADOW_CASTERS in Light.h.
const int MAX_SPOT_LIGHTS = 8;
const int MAX_SPOT_SHADOW_CASTERS = 3;

struct SpotLightGPU {
    vec4 position;   // xyz = world position, w = range
    vec4 direction;  // xyz = normalized direction, w = intensity
    vec4 color;      // rgb = color
    vec4 params;     // x = cos(inner), y = cos(outer), z = shadowIndex (-1 = none), w unused
};

layout(set = 0, binding = 5) uniform SpotLightUBO {
    mat4 shadowMatrices[3];
    SpotLightGPU lights[8];
    ivec4 counts; // x = active light count
} spotLights;

layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D metallicMap;
layout(set = 1, binding = 3) uniform sampler2D roughnessMap;
layout(set = 1, binding = 4) uniform sampler2D aoMap;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 emissive;
} push;

layout(location = 0) out vec4 outColor;

// ── Shadow PCF ──────────────────────────────────────────────
// Use the geometric normal for the receiver bias. Normal-map detail must not
// alter shadow depth or it can turn texture detail into visible shadow bands.
float SampleShadowPCF(sampler2DShadow shadowMap, vec4 shadowCoord, float NdotL) {
    vec3 proj = shadowCoord.xyz / shadowCoord.w;
    proj.xy   = proj.xy * 0.5 + 0.5;

    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 ||
    proj.y < 0.0 || proj.y > 1.0) {
        return 1.0;
    }

    // Apply a small bias directly to the receiver comparison depth. This avoids
    // the per-triangle offsets produced by negative raster slope bias.
    float slopeScale = clamp(1.0 - NdotL, 0.0, 1.0);
    float bias       = mix(0.00001, 0.0001, slopeScale);
    float biasedZ    = proj.z - bias;

    float shadow   = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));

    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            shadow += texture(shadowMap,
                              vec3(proj.xy + vec2(x, y) * texelSize, biasedZ));
        }
    }
    return shadow / 9.0;
}

// ── Cook-Torrance BRDF helpers ───────────────────────────────
float D_GGX(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float G_SchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float G_Smith(float NdotV, float NdotL, float roughness) {
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

vec3 F_Schlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Roughness-aware Fresnel for ambient/IBL use: at grazing angles, a rough
// surface doesn't show as strong a bright rim as a smooth one, unlike plain
// F_Schlick which assumes a single incident light direction.
vec3 F_SchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    // ── Sample textures ──────────────────────────────────────
    vec4 albedoSample   = texture(albedoMap,    inTexCoord);
    vec3 albedo         = albedoSample.rgb;
    float metallic  = texture(metallicMap,  inTexCoord).b;
    float roughness = texture(roughnessMap, inTexCoord).g;
    float ao            = texture(aoMap,        inTexCoord).r;

    // ── Normal mapping ────────────────────────────────────────
    vec3 normalSample = texture(normalMap, inTexCoord).rgb * 2.0 - 1.0;
    vec3 geometricNormal = normalize(inNormal);
    vec3 tangent = inTangent - geometricNormal * dot(geometricNormal, inTangent);
    tangent = normalize(tangent);
    float handedness = dot(cross(geometricNormal, tangent), inBitangent) < 0.0 ? -1.0 : 1.0;
    vec3 bitangent = normalize(cross(geometricNormal, tangent)) * handedness;
    mat3 TBN = mat3(tangent, bitangent, geometricNormal);
    vec3 N = normalize(TBN * normalSample);


    // ── Vectors ───────────────────────────────────────────────
    vec3 V = normalize(camera.cameraPosition.xyz - inWorldPos);
    vec3 L        = normalize(-camera.lightDirection.xyz);
    vec3 H        = normalize(V + L);

    float NdotL   = max(dot(N, L), 0.0);
    float shadowNdotL = max(dot(geometricNormal, L), 0.0);
    float NdotV   = max(dot(N, V), 0.0001);
    float NdotH   = max(dot(N, H), 0.0);
    float HdotV   = max(dot(H, V), 0.0);

    // ── F0 — base reflectivity ────────────────────────────────
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // ── Cook-Torrance BRDF ────────────────────────────────────
    float D   = D_GGX(NdotH, roughness);
    float G   = G_Smith(NdotV, NdotL, roughness);
    vec3  F   = F_Schlick(HdotV, F0);

    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    vec3 lightColor = vec3(3.0);
    vec3 Lo = (diffuse + specular) * lightColor * NdotL;


    // ── Shadow ────────────────────────────────────────────────
    float depth = abs((camera.view * vec4(inWorldPos, 1.0)).z);

    float shadow;
    if (depth < abs(camera.cascadeSplits.x)) {
        shadow = SampleShadowPCF(shadowMap0, inShadowCoord0, shadowNdotL);
    } else if (depth < abs(camera.cascadeSplits.y)) {
        shadow = SampleShadowPCF(shadowMap1, inShadowCoord1, shadowNdotL);
    } else {
        shadow = SampleShadowPCF(shadowMap2, inShadowCoord2, shadowNdotL);
    }

    Lo *= shadow;

    // ── Spot lights ───────────────────────────────────────────
    int spotCount = min(spotLights.counts.x, MAX_SPOT_LIGHTS);
    for (int i = 0; i < spotCount; i++) {
        SpotLightGPU sl = spotLights.lights[i];

        vec3 toSpotLight = sl.position.xyz - inWorldPos;
        float spotDist    = length(toSpotLight);
        vec3 Lspot         = toSpotLight / max(spotDist, 0.0001);
        vec3 spotDir       = normalize(sl.direction.xyz);

        float cosInner = sl.params.x;
        float cosOuter = sl.params.y;
        int shadowIndex     = int(sl.params.z);
        float spotRange     = sl.position.w;
        float spotIntensity = sl.direction.w;

        float cosTheta  = dot(-Lspot, spotDir);
        float coneAtten = clamp((cosTheta - cosOuter) / max(cosInner - cosOuter, 0.0001), 0.0, 1.0);
        coneAtten *= coneAtten;

        float distAtten = clamp(1.0 - (spotDist / max(spotRange, 0.0001)), 0.0, 1.0);
        distAtten *= distAtten;

        if (coneAtten <= 0.0 || distAtten <= 0.0) continue;

        vec3 Hspot      = normalize(V + Lspot);
        float NdotLspot = max(dot(N, Lspot), 0.0);
        float shadowNdotLspot = max(dot(geometricNormal, Lspot), 0.0);
        float NdotHspot = max(dot(N, Hspot), 0.0);
        float HdotVspot = max(dot(Hspot, V), 0.0);

        float Dspot = D_GGX(NdotHspot, roughness);
        float Gspot = G_Smith(NdotV, NdotLspot, roughness);
        vec3  Fspot = F_Schlick(HdotVspot, F0);

        vec3 specularSpot = (Dspot * Gspot * Fspot) / max(4.0 * NdotV * NdotLspot, 0.001);
        vec3 kDspot        = (vec3(1.0) - Fspot) * (1.0 - metallic);
        vec3 diffuseSpot    = kDspot * albedo / PI;

        // Constant-indexed to avoid dynamic sampler array indexing.
        float spotShadow = 1.0;
        if (shadowIndex == 0) {
            spotShadow = SampleShadowPCF(shadowMapSpot[0], inShadowCoordSpot[0], shadowNdotLspot);
        } else if (shadowIndex == 1) {
            spotShadow = SampleShadowPCF(shadowMapSpot[1], inShadowCoordSpot[1], shadowNdotLspot);
        } else if (shadowIndex == 2) {
            spotShadow = SampleShadowPCF(shadowMapSpot[2], inShadowCoordSpot[2], shadowNdotLspot);
        }

        vec3 spotLightColor = sl.color.rgb * spotIntensity;
        Lo += (diffuseSpot + specularSpot) * spotLightColor * NdotLspot * coneAtten * distAtten * spotShadow;
    }

    // ── Ambient (IBL) ────────────────────────────────────────
    float exposure = camera.cascadeSplits.w;

    vec3 Fambient = F_SchlickRoughness(NdotV, F0, roughness);
    vec3 kSambient = Fambient;
    vec3 kDambient = (1.0 - kSambient) * (1.0 - metallic);

    vec3 irradiance = texture(irradianceMap, N).rgb * exposure;
    vec3 diffuseIBL = irradiance * albedo;

    vec3 R = reflect(-V, N);
    vec3 prefilteredColor = textureLod(prefilteredEnvMap, R, roughness * MAX_REFLECTION_LOD).rgb * exposure;
    vec2 envBRDF = texture(brdfLUT, vec2(NdotV, roughness)).rg;
    vec3 specularIBL = prefilteredColor * (Fambient * envBRDF.x + envBRDF.y);

    vec3 ambient = (kDambient * diffuseIBL + specularIBL) * ao;

    // ── Final color ───────────────────────────────────────────
    // Shadow maps only physically block direct light, but leaving ambient untouched in shadow
    // (shadow==0) reads as too flat/bright for this engine's stylistic target, so dim it partway
    // instead of fully to black, which would look unnaturally harsh for an IBL-lit scene.
    vec3 color = ambient * mix(0.5, 1.0, shadow) + Lo;
    color += max(push.emissive.rgb, vec3(0.0)) * max(push.emissive.a, 0.0);


    // Tone mapping (ACES filmic approximation)
    color = (color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14);
    color = clamp(color, 0.0, 1.0);

    // No manual gamma correction here: the swapchain is VK_FORMAT_B8G8R8A8_SRGB,
    // so the hardware already linear->sRGB encodes this on write. Gamma-correcting
    // here too would double-encode (crushes everything toward white).
    outColor = vec4(color, albedoSample.a);
}
