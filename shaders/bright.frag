#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneTex;

layout(push_constant) uniform PC {
    float threshold;
    float knee;
    float pad0;
    float pad1;
} pc;

void main() {
    vec3 c = texture(sceneTex, vUV).rgb;
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float w = smoothstep(pc.threshold, pc.threshold + pc.knee, lum);
    outColor = vec4(c * w, 1.0);
}
