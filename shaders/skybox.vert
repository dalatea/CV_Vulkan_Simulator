#version 450

layout(location = 0) out vec3 vDir;

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

layout(location = 0) in vec3 position;

void main() {
    mat4 viewRot = mat4(mat3(ubo.view));
    vec4 posClip = ubo.projection * viewRot * vec4(position, 1.0);

    gl_Position = vec4(posClip.x, posClip.y, posClip.w, posClip.w);

    vDir = position;
}