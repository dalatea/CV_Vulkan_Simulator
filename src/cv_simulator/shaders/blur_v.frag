#version 450
layout(location=0) in vec2 vUV;
layout(location=0) out vec4 outColor;

layout(set=0,binding=0) uniform sampler2D img;

layout(push_constant) uniform PC {
    vec2 texelSize;
    float radius;
    float pad0;
} pc;

void main() {
    vec2 ts = pc.texelSize;
    vec3 sum = vec3(0.0);

    sum += texture(img, vUV + vec2(0, -4.0*ts.y)).rgb * 0.05;
    sum += texture(img, vUV + vec2(0, -3.0*ts.y)).rgb * 0.09;
    sum += texture(img, vUV + vec2(0, -2.0*ts.y)).rgb * 0.12;
    sum += texture(img, vUV + vec2(0, -1.0*ts.y)).rgb * 0.15;
    sum += texture(img, vUV).rgb                       * 0.18;
    sum += texture(img, vUV + vec2(0,  1.0*ts.y)).rgb * 0.15;
    sum += texture(img, vUV + vec2(0,  2.0*ts.y)).rgb * 0.12;
    sum += texture(img, vUV + vec2(0,  3.0*ts.y)).rgb * 0.09;
    sum += texture(img, vUV + vec2(0,  4.0*ts.y)).rgb * 0.05;

    outColor = vec4(sum, 1.0);
}