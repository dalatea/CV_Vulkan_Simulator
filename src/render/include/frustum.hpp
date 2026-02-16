#pragma once
#include <glm/glm.hpp>

struct Frustum {
    glm::vec4 planes[6];
};

inline Frustum extractFrustum(const glm::mat4& VP)
{
    Frustum f;

    f.planes[0] = VP[3] + VP[0]; // left
    f.planes[1] = VP[3] - VP[0]; // right
    f.planes[2] = VP[3] + VP[1]; // bottom
    f.planes[3] = VP[3] - VP[1]; // top
    f.planes[4] = VP[3] + VP[2]; // near
    f.planes[5] = VP[3] - VP[2]; // far

    for (int i = 0; i < 6; i++)
        f.planes[i] /= glm::length(glm::vec3(f.planes[i]));

    return f;
}

inline bool isVisible(const Frustum& f, glm::vec3 center, float radius)
{
    for (int i = 0; i < 6; i++) {
        if (glm::dot(glm::vec3(f.planes[i]), center) + f.planes[i].w < -radius)
            return false;
    }
    return true;
}
