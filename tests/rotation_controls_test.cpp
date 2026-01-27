#include "rotation_controls.h"

#include <array>
#include <cassert>
#include <cmath>
#include <iostream>

using raidbuilder::AxisId;
using raidbuilder::RollDecision;

namespace {

enum Dir {
    PX = 0,  // +X (right)
    NX = 1,  // -X (left)
    PY = 2,  // +Y (up)
    NY = 3,  // -Y (down)
    PZ = 4,  // +Z (back)
    NZ = 5,  // -Z (front)
};

struct CubeOrientation {
    // label_by_dir[Dir] = face label currently pointing in that direction.
    std::array<int, 6> label_by_dir{};

    int Front() const { return label_by_dir[NZ]; }
    int Back() const { return label_by_dir[PZ]; }
    int Right() const { return label_by_dir[PX]; }
    int Left() const { return label_by_dir[NX]; }
    int Up() const { return label_by_dir[PY]; }
    int Down() const { return label_by_dir[NY]; }
};

static CubeOrientation MakeStart() {
    CubeOrientation c;
    // Start: front=1, back=3, right=2, left=4, up=5, down=6.
    c.label_by_dir[NZ] = 1;
    c.label_by_dir[PZ] = 3;
    c.label_by_dir[PX] = 2;
    c.label_by_dir[NX] = 4;
    c.label_by_dir[PY] = 5;
    c.label_by_dir[NY] = 6;
    return c;
}

// Rotate around global Y in 90-degree steps. turns=-1 means -90 degrees.
static void RotateY(CubeOrientation* c, int turns) {
    while (turns != 0) {
        CubeOrientation prev = *c;
        if (turns < 0) {
            // -90 deg around Y: front <- right <- back <- left <- front.
            c->label_by_dir[NZ] = prev.label_by_dir[PX];
            c->label_by_dir[PX] = prev.label_by_dir[PZ];
            c->label_by_dir[PZ] = prev.label_by_dir[NX];
            c->label_by_dir[NX] = prev.label_by_dir[NZ];
            ++turns;
        } else {
            // +90 deg around Y: front <- left <- back <- right <- front.
            c->label_by_dir[NZ] = prev.label_by_dir[NX];
            c->label_by_dir[NX] = prev.label_by_dir[PZ];
            c->label_by_dir[PZ] = prev.label_by_dir[PX];
            c->label_by_dir[PX] = prev.label_by_dir[NZ];
            --turns;
        }
        // up/down unchanged
        c->label_by_dir[PY] = prev.label_by_dir[PY];
        c->label_by_dir[NY] = prev.label_by_dir[NY];
    }
}

// Rotate around global X in 90-degree steps. turns=-1 means -90 degrees.
static void RotateX(CubeOrientation* c, int turns) {
    while (turns != 0) {
        CubeOrientation prev = *c;
        if (turns < 0) {
            // -90 deg around X: front <- down <- back <- up <- front.
            c->label_by_dir[NZ] = prev.label_by_dir[NY];
            c->label_by_dir[NY] = prev.label_by_dir[PZ];
            c->label_by_dir[PZ] = prev.label_by_dir[PY];
            c->label_by_dir[PY] = prev.label_by_dir[NZ];
            ++turns;
        } else {
            // +90 deg around X: front <- up <- back <- down <- front.
            c->label_by_dir[NZ] = prev.label_by_dir[PY];
            c->label_by_dir[PY] = prev.label_by_dir[PZ];
            c->label_by_dir[PZ] = prev.label_by_dir[NY];
            c->label_by_dir[NY] = prev.label_by_dir[NZ];
            --turns;
        }
        // left/right unchanged
        c->label_by_dir[PX] = prev.label_by_dir[PX];
        c->label_by_dir[NX] = prev.label_by_dir[NX];
    }
}

// Rotate around global Z in 90-degree steps. turns=-1 means -90 degrees.
static void RotateZ(CubeOrientation* c, int turns) {
    while (turns != 0) {
        CubeOrientation prev = *c;
        if (turns < 0) {
            // -90 deg around Z: right <- up <- left <- down <- right.
            c->label_by_dir[PX] = prev.label_by_dir[PY];
            c->label_by_dir[PY] = prev.label_by_dir[NX];
            c->label_by_dir[NX] = prev.label_by_dir[NY];
            c->label_by_dir[NY] = prev.label_by_dir[PX];
            ++turns;
        } else {
            // +90 deg around Z: right <- down <- left <- up <- right.
            c->label_by_dir[PX] = prev.label_by_dir[NY];
            c->label_by_dir[NY] = prev.label_by_dir[NX];
            c->label_by_dir[NX] = prev.label_by_dir[PY];
            c->label_by_dir[PY] = prev.label_by_dir[PX];
            --turns;
        }
        // front/back unchanged
        c->label_by_dir[NZ] = prev.label_by_dir[NZ];
        c->label_by_dir[PZ] = prev.label_by_dir[PZ];
    }
}

static void ApplyRollFromFace(CubeOrientation* c, float nx, float ny, float nz, bool roll_up) {
    const RollDecision roll = raidbuilder::DecideRollFromFace(nx, ny, nz, roll_up);

    // Map decision to discrete 90-degree turns.
    const int turns = (roll.delta_deg < 0.0f) ? -1 : 1;
    if (roll.axis == AxisId::X) {
        RotateX(c, turns);
    } else if (roll.axis == AxisId::Y) {
        RotateY(c, turns);
    } else {
        RotateZ(c, turns);
    }
}

// Human-readable input helpers.
static void PressLeft(CubeOrientation* c) { RotateY(c, -1); }
static void PressRight(CubeOrientation* c) { RotateY(c, 1); }
static void PressUp(CubeOrientation* c) {
    // Treat the face under the cursor as front; use the canonical front normal.
    ApplyRollFromFace(c, 0.0f, 0.0f, -1.0f, /*roll_up=*/true);
}
static void PressDown(CubeOrientation* c) {
    ApplyRollFromFace(c, 0.0f, 0.0f, -1.0f, /*roll_up=*/false);
}

}  // namespace

int main() {
    CubeOrientation c = MakeStart();

    // Human-readable scenario from the spec:
    // Start: front=1.
    assert(c.Front() == 1);

    // Sequence: Left, Left, Up, Left, Left, Up
    // We intentionally go longer to catch sign flips that only show up later.
    PressLeft(&c);
    assert(c.Front() == 2);

    PressLeft(&c);
    assert(c.Front() == 3);

    PressUp(&c);
    assert(c.Front() == 6);

    PressLeft(&c);
    assert(c.Front() == 4);

    PressUp(&c);
    assert(c.Front() == 1);

    std::cout << "rotation_controls_test: OK\n";
    return 0;
}
