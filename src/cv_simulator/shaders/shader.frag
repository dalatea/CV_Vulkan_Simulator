#version 450

layout (location = 0) in vec3 fragColor;
layout (location = 1) in vec3 fragPosWorld;
layout (location = 2) in vec3 fragNormalWorld;
//layout(location = 3) in vec4 fragPosLightSpace;
layout (location = 3) in vec2 fragTexCoord;  
layout (location = 4) in vec3 fragSpecularColor;
layout (location = 5) in float fragShininess;
layout (location = 6) in vec3 fragTangentWorld;

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

  PointLight pointLights[400];
  int numLights;

  float autoExposure;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D shadowMap;
layout(set = 1, binding = 0) uniform sampler2D objTexture;
layout(set = 1, binding = 1) uniform sampler2D normalMap;

layout(push_constant) uniform Push {
  mat4 modelMatrix;
  mat4 normalMatrix;
  vec4 materialParams;
} push;

vec3 applyNormalMap(vec3 N, vec3 T) {
    vec3 nTS = texture(normalMap, fragTexCoord).rgb * 2.0 - 1.0;

    if (length(T) < 1e-4) return N;

    T = normalize(T - N * dot(N, T));
    vec3 B = cross(N, T);
    mat3 TBN = mat3(T, B, N);

    return normalize(TBN * nTS);
}

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
  vec3 geomNormal = normalize(fragNormalWorld);
  vec3 surfaceNormal = applyNormalMap(geomNormal, fragTangentWorld);
  
  vec3 cameraPosWorld = ubo.invView[3].xyz;
  vec3 viewDirection = normalize(cameraPosWorld - fragPosWorld);

  float shin = max(fragShininess, 1.0); 
  vec3 sunDir = normalize(-ubo.sunDirection.xyz);
  float sunNdotL = max(dot(surfaceNormal, sunDir), 0.0);

  vec3 sunDiffuse = ubo.sunColor.rgb * ubo.sunColor.a * sunNdotL;

  vec3 sunHalf = normalize(sunDir + viewDirection);
  float sunBlinn = clamp(dot(surfaceNormal, sunHalf), 0.0, 1.0);
  sunBlinn = pow(sunBlinn, shin);

  sunBlinn *= step(0.001, sunNdotL);
  vec3 sunSpecular = ubo.sunColor.rgb * ubo.sunColor.a * sunBlinn * fragSpecularColor;

  float shadowFactor = computeShadow(fragPosWorld, geomNormal);
  sunDiffuse  *= shadowFactor;
  sunSpecular *= shadowFactor;

  vec3 diffusePL = vec3(0.0);
  vec3 specularPL = vec3(0.0);

  for (int i = 0; i < ubo.numLights; i++) {
    PointLight light = ubo.pointLights[i];
    vec3 directionToLight = light.position.xyz - fragPosWorld;
    float attenuation = 1.0 / dot(directionToLight, directionToLight);
    directionToLight = normalize(directionToLight);

    float cosAngIncidence = max(dot(surfaceNormal, directionToLight), 0);
    vec3 intensity = light.color.rgb * light.color.w * attenuation;

    diffusePL += intensity * cosAngIncidence;

    vec3 halfAngle = normalize(directionToLight + viewDirection);
    float blinnTerm = clamp(dot(surfaceNormal, halfAngle), 0.0, 1.0);
    blinnTerm = pow(blinnTerm, shin);
    blinnTerm *= step(0.001, cosAngIncidence);
    specularPL += intensity * blinnTerm * fragSpecularColor;
  }

  vec3 ambient = ubo.ambientLightColor.rgb * ubo.ambientLightColor.w;
  vec3 diffuseLight = ambient + sunDiffuse + diffusePL;
  vec3 totalSpecular = sunSpecular + specularPL;
  
  vec4 texColor = texture(objTexture, fragTexCoord);

  vec3 finalColor = diffuseLight * fragColor * texColor.rgb 
                  + totalSpecular * fragColor;

  if (any(isnan(finalColor)) || any(isinf(finalColor))) {
      finalColor = diffuseLight * fragColor * texColor.rgb;
  }

  float matAlpha = push.materialParams.x;
  outColor = vec4(finalColor, texColor.a * matAlpha);
}
