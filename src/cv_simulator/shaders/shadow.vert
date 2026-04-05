#version 450

layout(location = 0) in vec3 position;

struct PointLight {
  vec4 position; // ignore w
  vec4 color; // w is intensity
};

layout(std140, set = 0, binding = 0) uniform GlobalUbo {
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

layout(push_constant) uniform Push {
    mat4 modelMatrix;
    mat4 normalMatrix;
} push;

void main() {
    vec4 worldPos   = push.modelMatrix * vec4(position, 1.0);
    gl_Position     = ubo.lightViewProj * worldPos;
}
