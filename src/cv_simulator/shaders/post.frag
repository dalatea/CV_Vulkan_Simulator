#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;
layout(set = 0, binding = 1) uniform sampler2D bloomTex;
layout(set = 0, binding = 3) uniform sampler2D sceneDepth;
layout(set = 0, binding = 4) uniform sampler2D lensFlareTex;
layout(set = 0, binding = 5) uniform sampler2D godRaysTex;

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

float computeSunOcclusion(vec2 sunUV) {
    if (any(lessThan(sunUV, vec2(0.0))) || any(greaterThan(sunUV, vec2(1.0))))
        return 0.0;

    const float SKY_DEPTH_THRESHOLD = 0.9995;
    const int   SAMPLES   = 12;
    const float RADIUS    = 0.03;   // радиус выборки в UV-пространстве
    float occVisible = 0.0;

    float d0 = texture(sceneDepth, sunUV).r;
    occVisible += (d0 >= SKY_DEPTH_THRESHOLD) ? 2.0 : 0.0;
    float totalWeight = 2.0;

    for (int i = 0; i < SAMPLES; ++i) {
        float angle = float(i) * (6.28318 / float(SAMPLES));
        float r        = (i % 2 == 0) ? RADIUS : RADIUS * 0.5;
        vec2  offset   = vec2(cos(angle), sin(angle)) * r;
        vec2  sampleUV = clamp(sunUV + offset, 0.001, 0.999);
        float d        = texture(sceneDepth, sampleUV).r;
        occVisible    += (d >= SKY_DEPTH_THRESHOLD) ? 1.0 : 0.0;
        totalWeight   += 1.0;
    }

    return occVisible / totalWeight;
}

float sunDiskMask(vec2 uv, vec2 sunUV, float r) {
    float d = length(uv - sunUV);
    return 1.0 - smoothstep(r * 0.9, r * 1.1, d);
}

float softDisk(vec2 uv, vec2 center, float radius, float softness) {
    float d = length(uv - center);
    return 1.0 - smoothstep(radius, radius + softness, d);
}

void main() {
    vec2 uv = clamp(vUV, 0.0, 1.0);
    vec3 color = texture(sceneColor, uv).rgb;
    //color *= ubo.autoExposure;

    vec2 sunUV = ubo.sunScreen.xy;
    float visScreen = clamp(ubo.sunScreen.z, 0.0, 1.0); 
    float viewFactor = clamp(ubo.sunParams.x, 0.0, 1.0);

    //float sunOcclusion = computeSunOcclusion(sunUV);

    //float vis = visScreen * sunOcclusion;
    float vis = visScreen;
    float scale = max(ubo.sunScreen.w, 0.0);

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
            vec3 s = texture(bloomTex, clamp(sampleUV, 0.0, 1.0)).rgb;

            rays += s * occ * illuminationDecay * weight;
            illuminationDecay *= decay;
        }

        float falloff = 1.0 - smoothstep(0.0, 0.9, dist);

        float coreMask = smoothstep(0.0, 0.015, dist);
        rays *= falloff * coreMask * exposure * vis * scale * viewFactor;
    }

    float distSun = length(uv - sunUV);

    float disk = sunDiskMask(uv, sunUV, SUN_R);
    float notDiskWide = smoothstep(SUN_R * 1.0, SUN_R * 2.0, distSun);
    
    vec3 rayTracedFlare = texture(lensFlareTex, uv).rgb;
    float sunExclude     = smoothstep(0.0, 0.001, distSun);

    rayTracedFlare *= vis * viewFactor * sunExclude;

    vec3 godRays = texture(godRaysTex, uv).rgb;
    godRays *= vis * viewFactor * 0.5; 
    
    color *= mix(1.0, 1.08, vis * viewFactor);

    color += rayTracedFlare;
    color += godRays;

    outColor = vec4(color, 1.0);
}

