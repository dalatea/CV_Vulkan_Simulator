#version 450

const vec2 OFFSETS[6] = vec2[](
  vec2(-1.0, -1.0),
  vec2(-1.0, 1.0),
  vec2(1.0, -1.0),
  vec2(1.0, -1.0),
  vec2(-1.0, 1.0),
  vec2(1.0, 1.0)
);

layout (location = 0) out vec2 fragOffset;

struct PointLight {
  vec4 position; // ignore w
  vec4 color; // w is intensity
};

layout(set = 0, binding = 0) uniform GlobalUbo {
  mat4 projection;
  mat4 view;
  mat4 invView;
  mat4 lightViewProj;

  vec4 ambientLightColor; // w is intensity

  vec4 sunDirection;
  vec4 sunColor;
  vec4 sunParams;
  vec4 sunScreen;
  
  PointLight pointLights[400];
  int numLights;

  float autoExposure;
} ubo;

layout(push_constant) uniform Push {
  vec4 position;
  vec4 color;
  float radius;
} push;


void main() {
  fragOffset = OFFSETS[gl_VertexIndex];
  vec3 cameraRightWorld = {ubo.view[0][0], ubo.view[1][0], ubo.view[2][0]};
  vec3 cameraUpWorld = {ubo.view[0][1], ubo.view[1][1], ubo.view[2][1]};
  vec3 cameraFwdWorld   = -vec3(ubo.view[0][2], ubo.view[1][2], ubo.view[2][2]);

  vec3 camPos  = ubo.invView[3].xyz;
  vec3 toLight = push.position.xyz - camPos;
  float dist   = length(toLight);

  float minDist = push.radius + 0.12;
  vec3  effectivePos    = push.position.xyz;
  float effectiveRadius = push.radius;

  if (dist < minDist) {
     vec3 dir = (dist > 0.001) ? (toLight / dist) : cameraFwdWorld;
     effectivePos = camPos + dir * minDist;
     effectiveRadius = push.radius * (minDist / max(dist, 0.001));
     effectiveRadius = min(effectiveRadius, push.radius * 4.0);
  }

  vec3 positionWorld = effectivePos
    + effectiveRadius * fragOffset.x * cameraRightWorld
    + effectiveRadius * fragOffset.y * cameraUpWorld;


  gl_Position = ubo.projection * ubo.view * vec4(positionWorld, 1.0);
}