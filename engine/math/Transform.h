#pragma once
// ==============================================================================
//  DemonEngine::Transform
//  GLM-based transform with TRS decomposition.
// ==============================================================================
#include "core/DemonPCH.h"

namespace Demon {

struct Transform {
    glm::vec3 position    {0.0f};
    glm::quat rotation    {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale       {1.0f};

    // ── Matrix ────────────────────────────────────────────────────────────────
    [[nodiscard]] glm::mat4 toMatrix() const {
        return glm::translate(glm::mat4(1.0f), position)
             * glm::mat4_cast(rotation)
             * glm::scale(glm::mat4(1.0f), scale);
    }

    static Transform fromMatrix(const glm::mat4& m) {
        Transform t;
        glm::vec3 skew; glm::vec4 persp;
        glm::decompose(m, t.scale, t.rotation, t.position, skew, persp);
        return t;
    }

    // Matrix export helper for editor transform tools.
    void toImGuizmoMatrix(float out[16]) const {
        auto m = toMatrix();
        std::memcpy(out, glm::value_ptr(m), sizeof(float) * 16);
    }

    // ── Convenience ───────────────────────────────────────────────────────────
    [[nodiscard]] glm::vec3 forward() const { return rotation * glm::vec3(0,0,-1); }
    [[nodiscard]] glm::vec3 right()   const { return rotation * glm::vec3(1,0, 0); }
    [[nodiscard]] glm::vec3 up()      const { return rotation * glm::vec3(0,1, 0); }

    void setEulerAngles(glm::vec3 eulerDeg) {
        rotation = glm::quat(glm::radians(eulerDeg));
    }
    [[nodiscard]] glm::vec3 getEulerAngles() const {
        return glm::degrees(glm::eulerAngles(rotation));
    }

    void lookAt(const glm::vec3& target, const glm::vec3& worldUp = {0,1,0}) {
        glm::vec3 dir = glm::normalize(target - position);
        if (glm::length(dir) < 1e-6f) return;
        glm::mat4 m   = glm::lookAt(position, target, worldUp);
        rotation      = glm::quat_cast(glm::inverse(m));
    }

    // ── Interpolation ─────────────────────────────────────────────────────────
    static Transform lerp(const Transform& a, const Transform& b, float t) {
        return {
            glm::mix(a.position, b.position, t),
            glm::slerp(a.rotation, b.rotation, t),
            glm::mix(a.scale, b.scale, t)
        };
    }
};

} // namespace Demon
