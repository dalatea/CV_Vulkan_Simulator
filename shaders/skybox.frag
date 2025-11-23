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
  
  PointLight pointLights[10];
  int numLights;
} ubo;

layout(set = 0, binding = 2) uniform samplerCube skyboxMap;

void main() {
    vec3 dir = normalize(vDir);
    //dir.y = -dir.y;
    //dir.z = -dir.z;
    vec4 texColor = texture(skyboxMap, dir);

    if (texColor.a < 0.01) {
        texColor = vec4(0.1, 0.4, 0.8, 1.0);
    }

    outColor = texColor;
}
