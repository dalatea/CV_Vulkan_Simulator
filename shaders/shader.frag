#version 450

layout (location = 0) in vec3 fragColor;
layout (location = 1) in vec3 fragPosWorld;
layout (location = 2) in vec3 fragNormalWorld;
//layout(location = 3) in vec4 fragPosLightSpace;

layout (location = 0) out vec4 outColor;

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

  PointLight pointLights[10];
  int numLights;

  float autoExposure;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D shadowMap;

layout(push_constant) uniform Push {
  mat4 modelMatrix;
  mat4 normalMatrix;
} push;


vec3 applySunLight(vec3 normal) {
    vec3 L = normalize(-ubo.sunDirection.xyz);
    float NdotL = max(dot(normal, L), 0.0);

    return ubo.sunColor.rgb * ubo.sunColor.a * NdotL;
}

float computeShadow(vec3 worldPos, vec3 normal) {
    vec3 L = normalize(-ubo.sunDirection.xyz);
    float ndotl = max(dot(normal, L), 0.0);

    float normalOffset = 0.0015;
    vec3 biasedWorldPos = worldPos + normal * normalOffset;
    //vec3 biasedWorldPos = fragPosWorld;

    vec4 posLightSpace = ubo.lightViewProj * vec4(biasedWorldPos, 1.0);

    vec3 projCoords = posLightSpace.xyz / posLightSpace.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    if (projCoords.x < 0.0  || projCoords.x > 1.0 || 
        projCoords.y < 0.0 || projCoords.y > 1.0 || 
        projCoords.z < 0.0 || projCoords.z > 1.0) {
        return 1.0;
    }

    float bias = max(0.0005 * (1.0 - dot(normal, L)), 0.0005);
    //float bias = 0.0;

    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    float currentDepth = projCoords.z;

    float sum = 0.0;
    for (int x = -1; x <= 1; x++) {
      for (int y = -1; y <= 1; y++) {
        float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x,y) * texelSize).r;
        sum += (currentDepth - bias > pcfDepth) ? 0.0 : 1.0;
      }
    }

    return sum / 9.0;
}

void main() {
  vec3 surfaceNormal = normalize(fragNormalWorld);
  
  //vec3 L = normalize(ubo.sunDirection.xyz);
  /*vec4 posLightSpace = ubo.lightViewProj * vec4(fragPosWorld, 1.0);

  vec3 projCoords = posLightSpace.xyz / posLightSpace.w;
  projCoords.xy = projCoords.xy * 0.5 + 0.5;

  float d = texture(shadowMap, projCoords.xy).r;
  outColor = vec4(vec3(d), 1.0);
  //outColor = vec4(projCoords, 1.0);

  return;
*/

/*float s = computeShadow(fragPosWorld, surfaceNormal);
outColor = vec4(vec3(s), 1.0);
return;
*/
  vec3 ambient = ubo.ambientLightColor.rgb * ubo.ambientLightColor.w;
  
  vec3 sunLight = applySunLight(surfaceNormal);
  float shadowFactor = computeShadow(fragPosWorld, surfaceNormal);
  sunLight *= shadowFactor;

  vec3 diffusePL = vec3(0.0);
  vec3 specularLight = vec3(0.0);
  
  vec3 cameraPosWorld = ubo.invView[3].xyz;
  vec3 viewDirection = normalize(cameraPosWorld - fragPosWorld);

  for (int i = 0; i < ubo.numLights; i++) {
    PointLight light = ubo.pointLights[i];
    vec3 directionToLight = light.position.xyz - fragPosWorld;
    float attenuation = 1.0 / dot(directionToLight, directionToLight); // distance squared
    directionToLight = normalize(directionToLight);

    float cosAngIncidence = max(dot(surfaceNormal, directionToLight), 0);
    vec3 intensity = light.color.rgb * light.color.w * attenuation;

    diffusePL += intensity * cosAngIncidence;

    // specular lighting
    vec3 halfAngle = normalize(directionToLight + viewDirection);
    float blinnTerm = dot(surfaceNormal, halfAngle);
    blinnTerm = clamp(blinnTerm, 0, 1);
    blinnTerm = pow(blinnTerm, 5.0); // higher values -> sharper highlight
    specularLight += intensity * blinnTerm;
  }
  
  vec3 diffuseLight = ambient + sunLight + diffusePL;
  
  outColor = vec4(diffuseLight * fragColor + specularLight * fragColor, 1.0);
}
