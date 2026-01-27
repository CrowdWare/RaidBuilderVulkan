#pragma once

namespace raidbuilder {

enum class AxisId {
    X = 0,
    Y = 1,
    Z = 2,
};

struct RollDecision {
    AxisId axis;
    float delta_deg;
};

// Decide which global axis to roll around and which signed delta to apply.
// This depends only on camera direction, not on the selected face normal.
RollDecision DecideRollFromCamera(float right_x,
                                  float right_z,
                                  float forward_x,
                                  float forward_z,
                                  bool roll_up);

// Decide roll from the face under the cursor (treated as the "front" face).
RollDecision DecideRollFromFace(float nx, float ny, float nz, bool roll_up);

}  // namespace raidbuilder
