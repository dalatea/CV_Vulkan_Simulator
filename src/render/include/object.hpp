#pragma once

#include "model.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <memory>
#include <unordered_map>

namespace enginev {

    struct TransformComponent {
        glm::vec3 translation{};
        glm::vec3 scale{ 1.f, 1.f, 1.f };
        glm::vec3 rotation{};

        glm::mat4 mat4();
        glm::mat3 normalMatrix();
    };

    struct PointLightComponent {
        float lightIntensity = 1.0f;
    };

    class SimObject {
    public:
        using id_t = unsigned int;
        using Map = std::unordered_map<id_t, SimObject>;

        static SimObject createSimObject() {
            static id_t currentId = 0;
            return SimObject{ currentId++ };
        }

        static SimObject makePointLight(
            float intensity = 10.f, float radius = 0.1f, glm::vec3 color = glm::vec3(1.f));

        SimObject(const SimObject&) = delete;
        SimObject& operator=(const SimObject&) = delete;
        SimObject(SimObject&&) = default;
        SimObject& operator=(SimObject&&) = default;

        id_t getId() { return id; }

        std::shared_ptr<Model> model{};
        glm::vec3 color{};
        TransformComponent transform{};

        std::unique_ptr<PointLightComponent> pointLight = nullptr;

    private:
        SimObject(id_t objId) : id{ objId } {}

        id_t id;
    };
}