#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace enginev {
    struct LensSurfaceConfig {
        float radius;
        float z;
        float ior;
        float aperture;
        int   isStop;
    };

    struct ContaminationConfig {
        float dustDensity    = 0.0f;   // 0.0 = чисто, 1.0 = максимум пыли
        float smudgeAmount   = 0.0f;   // 0.0 - 1.0: размазывание / разводы
        float scratchAmount  = 0.0f;   // 0.0 - 1.0: царапины
        float waterDroplets  = 0.0f;   // 0.0 - 1.0: капли воды
        float scatterFactor  = 0.0f;   // усиление рассеяния из-за грязи 
    };

    struct CameraConfig {
        int         id            = 0;
        std::string name          = "camera";
        std::string controlType   = "keyboard"; 

        glm::vec3   position      = {0.f, 0.f, -2.5f};
        glm::vec3   rotation      = {0.f, 0.f, 0.f};

        // Параметры сенсора
        int         resolutionW   = 1280;
        int         resolutionH   = 720;
        float       fovDeg        = 50.0f;
        float       nearClip      = 0.1f;
        float       farClip       = 100.0f;

        // Параметры линз
        float       sensorZ       = 0.075f;
        float       sensorW       = 0.036f;
        float       sensorH       = 0.024f;
        std::vector<LensSurfaceConfig> lensSurfaces;

        // Загрязнение
        ContaminationConfig contamination;

        // ROS topic name 
        std::string rosTopic      = "";
    };

    struct LensParamsGPU {
        int   surfaceCount;
        float sensorZ;
        float sensorW;
        float sensorH;

        // Параметры загрязнения
        float dustDensity;
        float smudgeAmount;
        float scratchAmount;
        float waterDroplets;
        float scatterFactor;

        float pad0, pad1, pad2; 
    };

    struct LensSurfaceGPU {
        float radius;
        float z;
        float ior;
        float aperture;
        int   isStop;
        float pad0, pad1, pad2; 
    };

} 