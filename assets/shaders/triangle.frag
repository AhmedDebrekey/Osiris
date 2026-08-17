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
layout(location = 8) in vec4 inShadowCoordSpot;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 lightSpaceMatrices[3];
    vec4 cascadeSplits;
    vec4 lightDirection;
    vec4 cameraPosition;
    mat4 spotLightSpaceMatrix;
    vec4 spotPosition;
    vec4 spotDirection;
    vec4 spotParams; // x = cos(inner), y = cos(outer), z = range, w = intensity
    vec4 spotColor;  // rgb = color
} camera;

layout(set = 0, binding = 1) uniform sampler2DShadow shadowMap0;
layout(set = 0, binding = 2) uniform sampler2DShadow shadowMap1;
layout(set = 0, binding = 3) uniform sampler2DShadow shadowMap2;
layout(set = 0, binding = 4) uniform sampler2DShadow shadowMapSpot;

layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D metallicMap;
layout(set = 1, binding = 3) uniform sampler2D roughnessMap;
layout(set = 1, binding = 4) uniform sampler2D aoMap;

layout(location = 0) out vec4 outColor;

// ── Shadow PCF ──────────────────────────────────────────────
// NdotL biases the comparison: surfaces the light grazes at a steep angle
// need much more depth bias to avoid self-shadowing acne than surfaces the
// light hits head-on. A single flat bias can't satisfy both at once — too
// small and grazing surfaces (e.g. walls under a near-overhead light) acne,
// too large and head-on surfaces (e.g. a floor under the same light) show
// visible peter-panning.
float SampleShadowPCF(sampler2DShadow shadowMap, vec4 shadowCoord, float NdotL) {
    vec3 proj = shadowCoord.xyz / shadowCoord.w;
    proj.xy   = proj.xy * 0.5 + 0.5;

    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 ||
    proj.y < 0.0 || proj.y > 1.0) {
        return 1.0;
    }

    // NDC-space bias, independent of the depth format's precision curve.
    // VK_FORMAT_D32_SFLOAT makes the rasterizer's hardware depth bias (see the
    // shadow pipeline's depthBiasConstant/Slope) resolve to a near-zero offset
    // near 0.0, so it isn't enough on its own to prevent self-shadowing acne.
    float slopeScale = clamp(1.0 - NdotL, 0.0, 1.0);
    float bias       = mix(0.0008, 0.03, slopeScale);
    float biasedZ    = proj.z;

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

void main() {
    // ── Sample textures ──────────────────────────────────────
    vec4 albedoSample   = texture(albedoMap,    inTexCoord);
    vec3 albedo         = pow(albedoSample.rgb, vec3(2.2)); // sRGB to linear
    float metallic  = texture(metallicMap,  inTexCoord).b;
    float roughness = texture(roughnessMap, inTexCoord).g;
    float ao            = texture(aoMap,        inTexCoord).r;

    // ── Normal mapping ────────────────────────────────────────
    vec3 normalSample = texture(normalMap, inTexCoord).rgb * 2.0 - 1.0;
    mat3 TBN = mat3(normalize(inTangent), normalize(inBitangent), normalize(inNormal));
    vec3 N   = normalize(TBN * normalSample); //TODO: use the TBN not inNormal normalize(TBN * normalSample)


    // ── Vectors ───────────────────────────────────────────────
    vec3 V = normalize(camera.cameraPosition.xyz - inWorldPos);
    vec3 L        = normalize(-camera.lightDirection.xyz);
    vec3 H        = normalize(V + L);

    float NdotL   = max(dot(N, L), 0.0);
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
        shadow = SampleShadowPCF(shadowMap0, inShadowCoord0, NdotL);
    } else if (depth < abs(camera.cascadeSplits.y)) {
        shadow = SampleShadowPCF(shadowMap1, inShadowCoord1, NdotL);
    } else {
        shadow = SampleShadowPCF(shadowMap2, inShadowCoord2, NdotL);
    }

    Lo *= shadow;

    // ── Spot light ────────────────────────────────────────────
    vec3 toSpotLight  = camera.spotPosition.xyz - inWorldPos;
    float spotDist    = length(toSpotLight);
    vec3 Lspot         = toSpotLight / max(spotDist, 0.0001);
    vec3 spotDir       = normalize(camera.spotDirection.xyz);

    float cosInner = camera.spotParams.x;
    float cosOuter = camera.spotParams.y;
    float spotRange     = camera.spotParams.z;
    float spotIntensity = camera.spotParams.w;

    float cosTheta  = dot(-Lspot, spotDir);
    float coneAtten = clamp((cosTheta - cosOuter) / max(cosInner - cosOuter, 0.0001), 0.0, 1.0);
    coneAtten *= coneAtten;

    float distAtten = clamp(1.0 - (spotDist / max(spotRange, 0.0001)), 0.0, 1.0);
    distAtten *= distAtten;

    vec3 Hspot      = normalize(V + Lspot);
    float NdotLspot = max(dot(N, Lspot), 0.0);
    float NdotHspot = max(dot(N, Hspot), 0.0);
    float HdotVspot = max(dot(Hspot, V), 0.0);

    float Dspot = D_GGX(NdotHspot, roughness);
    float Gspot = G_Smith(NdotV, NdotLspot, roughness);
    vec3  Fspot = F_Schlick(HdotVspot, F0);

    vec3 specularSpot = (Dspot * Gspot * Fspot) / max(4.0 * NdotV * NdotLspot, 0.001);
    vec3 kDspot        = (vec3(1.0) - Fspot) * (1.0 - metallic);
    vec3 diffuseSpot    = kDspot * albedo / PI;

    float spotShadow = SampleShadowPCF(shadowMapSpot, inShadowCoordSpot, NdotLspot);

    vec3 spotLightColor = camera.spotColor.rgb * spotIntensity;
    Lo += (diffuseSpot + specularSpot) * spotLightColor * NdotLspot * coneAtten * distAtten * spotShadow;

    // ── Ambient ───────────────────────────────────────────────
    vec3 ambient = vec3(0.1) * albedo * ao;

    // ── Final color ───────────────────────────────────────────
    vec3 color = ambient + Lo;


    // Tone mapping (Reinhard)
    color = (color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14);
    color = clamp(color, 0.0, 1.0);

    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, albedoSample.a);
}