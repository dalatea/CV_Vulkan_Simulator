#version 450

layout (location = 0) in vec3 fragColor;
layout (location = 1) in vec3 fragPosWorld;
layout (location = 2) in vec3 fragNormalWorld;
layout(location = 3) in vec4 fragPosLightSpace;

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
  
  PointLight pointLights[10];
  int numLights;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D shadowMap;

layout(push_constant) uniform Push {
  mat4 modelMatrix;
  mat4 normalMatrix;
} push;


vec3 applySunLight(vec3 normal) {
    // направление света вектором указывает ОТ солнца
    vec3 L = normalize(-ubo.sunDirection.xyz);
    float NdotL = max(dot(normal, L), 0.0);

    return ubo.sunColor.rgb * ubo.sunColor.a * NdotL;
}

float computeShadow(vec4 posLightSpace) {
    // из clip-space в NDC
    vec3 projCoords = posLightSpace.xyz / posLightSpace.w;
    // в [0,1]
    projCoords = projCoords * 0.5 + 0.5;

    // если вышли за пределы shadow map – считаем, что точка освещена
    if (projCoords.x < 0.0  || projCoords.x > 1.0 || 
        projCoords.y < 0.0 || projCoords.y > 1.0 || 
        projCoords.z < 0.0 || projCoords.z > 1.0) {
        return 1.0;
    }

    float closestDepth = texture(shadowMap, projCoords.xy).r; // глубина из карты
    float currentDepth = projCoords.z;

    // небольшой bias, чтобы убрать acne
    float bias = 0.001;
    // если текущая глубина заметно дальше, чем в карте, точка в тени
    float shadow = currentDepth - bias > closestDepth ? 0.0 : 1.0;
    return shadow;
}

void main() {
  vec3 surfaceNormal = normalize(fragNormalWorld);
  
  vec3 ambient = ubo.ambientLightColor.rgb * ubo.ambientLightColor.w;
  
  vec3 sunLight = applySunLight(surfaceNormal);
  float shadowFactor = computeShadow(fragPosLightSpace);
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
