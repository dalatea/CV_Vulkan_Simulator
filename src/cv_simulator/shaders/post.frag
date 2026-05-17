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

  vec4 sunParams;  
  vec4 sunScreen;   

  PointLight pointLights[400];
  int numLights;

  float autoExposure;
} ubo;

layout(push_constant) uniform ContaminationPC {
    float dustDensity;
    float smudgeAmount;
    float scratchAmount;
    float waterDroplets;
    float scatterFactor;
    float pad0, pad1, pad2;
} contam;

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

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float hash11(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

float dustOverlay(vec2 uv, float density) {
    if (density <= 0.0) return 1.0;
    float clean = 1.0;

    vec2 cell = floor(uv * 45.0);
    float r = hash21(cell);
    if (r < density * 0.4) {
        vec2 center = (cell + vec2(hash21(cell + 1.0), hash21(cell + 2.0))) / 45.0;
        float d = length(uv - center);
        float size = 0.004 + 0.012 * hash21(cell + 3.0);
        clean -= smoothstep(size, size * 0.15, d) * 0.35 * density;
    }

    cell = floor(uv * 180.0);
    r = hash21(cell + 50.0);
    if (r < density * 0.5) {
        vec2 center = (cell + vec2(hash21(cell + 5.0), hash21(cell + 6.0))) / 180.0;
        float d = length(uv - center);
        float size = 0.001 + 0.003 * hash21(cell + 7.0);
        clean -= smoothstep(size, 0.0, d) * 0.15 * density;
    }

    return max(clean, 1.0 - density * 0.45);
}

float smudgeOverlay(vec2 uv, float amount) {
    if (amount <= 0.0) return 1.0;

    vec2 cell = floor(uv * 8.0);
    float r = hash21(cell + 200.0);
    if (r < amount * 0.6) {
        vec2 center = (cell + vec2(hash21(cell + 201.0), hash21(cell + 202.0))) / 8.0;
        float d = length(uv - center);
        float size = 0.04 + 0.08 * hash21(cell + 203.0);
        float smudge = smoothstep(size, 0.0, d) * amount * 0.25;
        return 1.0 - smudge;
    }
    return 1.0;
}

float scratchOverlay(vec2 uv, float amount) {
    if (amount <= 0.0) return 0.0;
    float scratch = 0.0;
    for (int i = 0; i < 8; ++i) {
        float fi = float(i);
        if (hash11(fi + 0.5) > amount * 1.5) continue;

        float angle = hash11(fi * 7.13 + 0.5) * 3.14159;
        vec2 dir = vec2(cos(angle), sin(angle));
        vec2 center = vec2(hash11(fi * 3.7 + 1.0), hash11(fi * 5.3 + 1.0));

        float perp = abs(dot(uv - center, vec2(-dir.y, dir.x)));
        float proj = dot(uv - center, dir);
        float width = 0.0003 + 0.0007 * hash11(fi * 2.1 + 0.5);
        float len   = 0.05 + 0.2 * hash11(fi * 4.2 + 0.5);

        if (abs(proj) < len) {
            scratch += smoothstep(width, 0.0, perp) * 0.12;
        }
    }
    return scratch;
}

vec3 waterDropOverlay(vec2 uv, float amount, vec3 sceneCol) {
    if (amount <= 0.0) return vec3(0.0);
    vec3 effect = vec3(0.0);
    int numDrops = int(amount * 12.0);

    for (int i = 0; i < numDrops; ++i) {
        float fi = float(i);
        vec2 center = vec2(hash11(fi * 1.23 + 0.15), hash11(fi * 4.56 + 0.25));
        float baseRadius = 0.045 + 0.06 * hash11(fi * 7.89 + 0.5);

        vec2 delta = uv - center;
        float angle = atan(delta.y, delta.x);
        float wobble = 1.0
            + 0.15 * sin(angle * 3.0 + fi * 2.1)
            + 0.08 * sin(angle * 7.0 + fi * 5.3)
            + 0.05 * sin(angle * 13.0 + fi * 8.7);
        float radius = baseRadius * wobble;

        float d = length(delta);
        if (d > radius * 1.1) continue;

        float nd = d / radius;

        vec2 toPixel = delta / max(d, 0.0001);
        float refrPower = 0.015 * (1.0 - nd * nd);

        vec2 refractedUV;
        if (nd < 0.6) {
            refractedUV = center - toPixel * refrPower * 2.5;
        } else {
            float edgeFactor = smoothstep(0.6, 1.0, nd);
            refractedUV = uv + toPixel * refrPower * (1.0 + edgeFactor * 3.0);
        }

        refractedUV = clamp(refractedUV, 0.001, 0.999);
        vec3 refracted = texture(sceneColor, refractedUV).rgb;

        float dropMask = smoothstep(1.0, 0.75, nd);

        vec2 highlightPos = center - vec2(0.0, baseRadius * 0.25);
        float highlight = smoothstep(baseRadius * 0.2, 0.0, length(uv - highlightPos));
        highlight *= 0.12 * dropMask;

        float edgeDarken = smoothstep(0.7, 1.0, nd) * 0.08;
        vec3 dropColor = mix(sceneCol, refracted, dropMask * 0.8);
        dropColor += vec3(highlight);
        dropColor *= (1.0 - edgeDarken);

        effect += (dropColor - sceneCol) * dropMask;
    }
    return effect;
}

float dirtVignette(vec2 uv, float totalDirt) {
    if (totalDirt <= 0.0) return 1.0;
    float d = length(uv - 0.5) * 2.0;
    return 1.0 - smoothstep(0.4, 1.3, d) * totalDirt * 0.3;
}

void main() {
    vec2 uv = clamp(vUV, 0.0, 1.0);

    vec2 smudgeOff = vec2(0.0);
    if (contam.smudgeAmount > 0.0) {
        float nx = hash21(floor(uv * 12.0)) * 2.0 - 1.0;
        float ny = hash21(floor(uv * 12.0) + 100.0) * 2.0 - 1.0;
        smudgeOff = vec2(nx, ny) * contam.smudgeAmount * 0.006;
    }
    vec2 sampleUV_outer = clamp(uv + smudgeOff, 0.0, 1.0);

    vec3 color = texture(sceneColor, sampleUV_outer).rgb;
    //color *= ubo.autoExposure;

    vec2 sunUV = ubo.sunScreen.xy;
    float visScreen = clamp(ubo.sunScreen.z, 0.0, 1.0); 
    float viewFactor = clamp(ubo.sunParams.x, 0.0, 1.0);

    //float sunOcclusion = computeSunOcclusion(sunUV);

    //float vis = visScreen * sunOcclusion;
    float vis = visScreen;
    float scale = max(ubo.sunScreen.w, 0.0);

    if (vis <= 0.001) {
        
        if (contam.dustDensity > 0.0) {
            color *= dustOverlay(uv, contam.dustDensity);
        }
        if (contam.waterDroplets > 0.0) {
            vec3 origColor = texture(sceneColor, uv).rgb;
            color += waterDropOverlay(uv, contam.waterDroplets, origColor);
        }
        if (contam.scratchAmount > 0.0) {
            color += vec3(scratchOverlay(uv, contam.scratchAmount) * 0.03);
        }
        float totalDirt = contam.dustDensity + contam.smudgeAmount * 0.5;
        if (totalDirt > 0.0)
            color *= dirtVignette(uv, totalDirt);
        if (contam.smudgeAmount > 0.0) {
            float smudgeMask = smudgeOverlay(uv, contam.smudgeAmount);
            vec3 blurred = texture(bloomTex, sampleUV_outer).rgb;
            color = mix(color, mix(color, blurred, 0.3), (1.0 - smudgeMask));
        }
        if (contam.scatterFactor > 0.0) {
            float avgLum = luminance(color);
            color = mix(color, vec3(avgLum), contam.scatterFactor * 0.12);
        }
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
    
    vec3 rayTracedFlare = texture(lensFlareTex, sampleUV_outer).rgb;
    float sunExclude     = smoothstep(0.0, 0.001, distSun);

    rayTracedFlare *= vis * viewFactor * sunExclude;

    vec3 godRays = texture(godRaysTex, sampleUV_outer).rgb;
    
    godRays *= vis * viewFactor * 0.5; 
    
    color *= mix(1.0, 1.08, vis * viewFactor);

    color += rayTracedFlare;
    color += godRays;


    if (contam.dustDensity > 0.0) {
        float dustMask = dustOverlay(uv, contam.dustDensity);
        if (dustMask < 1.0) {
            float dustAmount = 1.0 - dustMask;
            float lum = luminance(color);
            if (lum > 1.5) {
                float cappedLum = mix(lum, 1.0, dustAmount * 0.7);
                color *= cappedLum / lum;
            } else {
                color *= dustMask;
            }
        }
    }

    if (contam.waterDroplets > 0.0) {
        vec3 origColor = texture(sceneColor, uv).rgb;
        color += waterDropOverlay(uv, contam.waterDroplets, origColor);
    }

    if (contam.scratchAmount > 0.0) {
        float scratch = scratchOverlay(uv, contam.scratchAmount);
        color += vec3(scratch * 0.03);
        color += ubo.sunColor.rgb * scratch * vis * viewFactor * 0.5;
    }

    if (contam.smudgeAmount > 0.0) {
        float smudgeMask = smudgeOverlay(uv, contam.smudgeAmount);
        vec3 blurred = texture(bloomTex, sampleUV_outer).rgb;
        color = mix(color, mix(color, blurred, 0.3), (1.0 - smudgeMask));
    }


    float totalDirt = contam.dustDensity + contam.smudgeAmount * 0.5;

    if (totalDirt > 0.0) {
        color *= dirtVignette(uv, totalDirt);
    }

    if (contam.scatterFactor > 0.0) {
        float avgLum = luminance(color);
        color = mix(color, vec3(avgLum), contam.scatterFactor * 0.12);
    }

    if (contam.dustDensity > 0.1) {
        vec3 scattered = texture(bloomTex, uv).rgb;
        color += scattered * contam.dustDensity * 0.15;
    }

    outColor = vec4(color, 1.0);
}

