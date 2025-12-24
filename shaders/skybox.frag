#version 450

layout(location = 0) in vec3 vDir;
layout(location = 0) out vec4 outColor;

struct PointLight {
  vec4 position; // ignore w
  vec4 color; // w is intensity
};

layout(set = 0, binding = 0) uniform GlobalUbo {
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

layout(set = 0, binding = 2) uniform samplerCube skyboxMap;

void main() {
    vec3 dir = normalize(vDir);

    vec3 sunDir = normalize(-ubo.sunDirection.xyz);
    vec4 texColor = texture(skyboxMap, dir);

    float sunDot = dot(dir, sunDir);

    float sunSize = 0.995;
    float core = smoothstep(sunSize, 1.0, sunDot);
    float glow = smoothstep(0.85, 1.0, sunDot);

    vec3 sunCol = ubo.sunColor.rgb * ubo.sunColor.a;

    texColor.rgb += core * sunCol * 5.0;
    texColor.rgb += glow * sunCol * 0.5;

    outColor = texColor;
}
