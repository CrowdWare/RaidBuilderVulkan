#include "rotation_controls.h"

#include <cmath>

namespace raidbuilder {

namespace {

static float SignOrOne(float v) {
    if (v > 0.0f) return 1.0f;
    if (v < 0.0f) return -1.0f;
    return 1.0f;
}

}  // namespace

RollDecision DecideRollFromCamera(float right_x,
                                  float right_z,
                                  float forward_x,
                                  float forward_z,
                                  bool roll_up) {
    // Choose the roll axis from the camera's right vector projected onto XZ.
    float ax = right_x;
    float az = right_z;
    float axis_len = std::sqrt(ax * ax + az * az);
    if (axis_len < 1e-3f) {
        // Fallback: use forward if right is degenerate.
        ax = forward_x;
        az = forward_z;
        axis_len = std::sqrt(ax * ax + az * az);
    }

    AxisId axis = (std::fabs(ax) >= std::fabs(az)) ? AxisId::X : AxisId::Z;

    float delta = roll_up ? -90.0f : 90.0f;
    // Use the camera-right sign on the chosen axis so "up" feels consistent on screen.
    float sign_source = (axis == AxisId::X) ? right_x : right_z;
    delta *= SignOrOne(sign_source);

    return RollDecision{axis, delta};
}

RollDecision DecideRollFromFace(float nx, float ny, float nz, bool roll_up) {
    // Treat the hovered face as the "front" and roll it toward up/down.
    // axis = up x normal
    float ax = nz;
    float az = -nx;
    float axis_len = std::sqrt(ax * ax + az * az);
    if (axis_len < 1e-3f) {
        // Degenerate (face points up/down): fall back to global X.
        ax = 1.0f;
        az = 0.0f;
    }

    AxisId axis = (std::fabs(ax) >= std::fabs(az)) ? AxisId::X : AxisId::Z;
    float sign_source = (axis == AxisId::X) ? ax : az;
    float delta = roll_up ? -90.0f : 90.0f;
    delta *= SignOrOne(sign_source);

    return RollDecision{axis, delta};
}

}  // namespace raidbuilder
