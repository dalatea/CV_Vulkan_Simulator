#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;
layout(set = 0, binding = 1) uniform sampler2D bloomTex;
layout(set = 0, binding = 3) uniform sampler2D sceneDepth;
layout(set = 0, binding = 4) uniform sampler2D lensFlareTex;

struct PointLight {
  vec4 position;
  vec4 color;
};

layout(set = 0, binding = 2) uniform GlobalUbo {
  mat4 projection;
  mat4 view;
  mat4 invView;
  mat4 lightViewProj;

  vec4 ambientLightColor;

  vec4 sunDirection;
  vec4 sunColor;

  vec4 sunParams;   // x = sunViewFactor
  vec4 sunScreen;   // xy = sunUV, z = visibility, w = intensityScale

  PointLight pointLights[400];
  int numLights;

  float autoExposure;
} ubo;

const float SUN_R = 0.995;
const float RING_W = 0.1;

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

float skyMask(vec2 uv) {
    float d = texture(sceneDepth, clamp(uv, 0.0, 1.0)).r;
    return smoothstep(0.999, 1.0, d);
}

float sunDiskMask(vec2 uv, vec2 sunUV, float r) {
    float d = length(uv - sunUV);
    return 1.0 - smoothstep(r * 0.9, r * 1.1, d);
}

float softDisk(vec2 uv, vec2 center, float radius, float softness) {
    float d = length(uv - center);
    return 1.0 - smoothstep(radius, radius + softness, d);
}

vec3 makeGhosts(vec2 uv, vec2 sunUV, vec3 bloomSampleBase) {
    vec2 center = vec2(0.5);
    vec2 dir = center - sunUV;

    vec3 acc = vec3(0.0);

    // позиции по лучу
    float p0 = 0.60;
    float p1 = 0.85;
    float p2 = 0.85;
    float p3 = 1.15; // может уйти дальше центра

    vec2 gPos0 = sunUV + dir * p0;
    vec2 gPos1 = sunUV + dir * p1;
    vec2 gPos2 = sunUV + dir * p2;
    vec2 gPos3 = sunUV + dir * p3;

    // размеры дисков
    float r0 = 0.035;
    float r1 = 0.025;
    float r2 = 0.045;
    float r3 = 0.020;

    float s0 = 0.015;
    float s1 = 0.012;
    float s2 = 0.018;
    float s3 = 0.010;

    float d0 = softDisk(uv, gPos0, r0, s0);
    float d1 = softDisk(uv, gPos1, r1, s1);
    float d2 = softDisk(uv, gPos2, r2, s2);
    float d3 = softDisk(uv, gPos3, r3, s3);

    // цвет берём из bloomTex в центре каждого ghost
    vec3 c0 = texture(bloomTex, clamp(gPos0, 0.0, 1.0)).rgb;
    vec3 c1 = texture(bloomTex, clamp(gPos1, 0.0, 1.0)).rgb;
    vec3 c2 = texture(bloomTex, clamp(gPos2, 0.0, 1.0)).rgb;
    vec3 c3 = texture(bloomTex, clamp(gPos3, 0.0, 1.0)).rgb;

    // чуть разный тон — выглядит “линзово”
    vec3 t0 = vec3(1.0, 0.9, 0.8);
    vec3 t1 = vec3(0.8, 0.9, 1.0);
    vec3 t2 = vec3(0.9, 1.0, 0.85);
    vec3 t3 = vec3(1.0, 0.85, 1.0);

    acc += c0 * t0 * d0 * 0.9;
    acc += c1 * t1 * d1 * 0.6;
    acc += c2 * t2 * d2 * 0.5;
    acc += c3 * t3 * d3 * 0.35;

    return acc;
}

void main() {
    vec2 uv = clamp(vUV, 0.0, 1.0);
    vec3 color = texture(sceneColor, uv).rgb;
    //color *= ubo.autoExposure;

    // --- параметры солнца на экране ---
    vec2 sunUV = ubo.sunScreen.xy;
    float vis  = clamp(ubo.sunScreen.z, 0.0, 1.0);
    float scale = max(ubo.sunScreen.w, 0.0);

    float viewFactor = clamp(ubo.sunParams.x, 0.0, 1.0);

    // если не смотрим на солнце — выходим без эффекта
    if (vis <= 0.001) {
        outColor = vec4(color, 1.0);
        return;
    }
    
    vec3 rays = vec3(0.0);
    {
        vec2 delta = uv - sunUV;
        float dist = length(delta);
        vec2 dirToPix = delta / max(dist, 1e-5);

        const int NUM_SAMPLES = 48;
        float density  = 0.9;
        float weight   = 0.08;
        float decay    = 0.98;
        float exposure = 0.5;

        vec2 sampleUV = uv;
        float illuminationDecay = 1.0;

        for (int i = 0; i < NUM_SAMPLES; ++i) {
            sampleUV -= dirToPix * (density / float(NUM_SAMPLES));
            
            float occ = skyMask(sampleUV);
            // ВОТ КЛЮЧЕВОЕ ИЗМЕНЕНИЕ:
            vec3 s = texture(bloomTex, clamp(sampleUV, 0.0, 1.0)).rgb;

            //rays += s * illuminationDecay * weight;
            rays += s * occ * illuminationDecay * weight;
            illuminationDecay *= decay;
        }

        float falloff = 1.0 - smoothstep(0.0, 0.9, dist);

        float coreMask = smoothstep(0.0, 0.015, dist);
        //rays *= falloff * coreMask * vis * scale * viewFactor * exposure;
        rays *= falloff * coreMask * exposure * vis * scale * viewFactor;
    }

    float distSun = length(uv - sunUV);

    float disk = sunDiskMask(uv, sunUV, SUN_R);
    float notDiskWide = smoothstep(SUN_R * 1.0, SUN_R * 2.0, distSun);
    vec3 ghosts = makeGhosts(uv, sunUV, vec3(1.0));
    ghosts *= vis * scale * viewFactor * notDiskWide;
    
    float halo = 1.0 - smoothstep(0.0, 0.15, distSun);
    halo *= smoothstep(0.0, 0.015, distSun);

    //vec3 haloCol = ubo.sunColor.rgb * halo * vis * viewFactor;
 
    color *= mix(1.0, 1.08, vis * viewFactor);

    //vec3 flare = texture(lensFlareTex, vUV).rgb;
    //color += flare;

    color += ghosts;
    color += rays;
    //color += haloCol;

    //outColor = texture(lensFlareTex, vUV);
    outColor = vec4(color, 1.0);
    //return;
}

