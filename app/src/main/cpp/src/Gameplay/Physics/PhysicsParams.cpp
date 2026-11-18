//	This file is part of the continued Journey MMORPG client
//	Copyright (C) 2015-2019  Daniel Allendorf, Ryan Payton
//
//	This program is free software: you can redistribute it and/or modify
//	it under the terms of the GNU Affero General Public License as published by
//	the Free Software Foundation, either version 3 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU Affero General Public License for more details.
//
//	You should have received a copy of the GNU Affero General Public License
//	along with this program.  If not, see <https://www.gnu.org/licenses/>.
#include "PhysicsParams.h"

#include <imgwz/node.hpp>
#include <imgwz/wz.hpp>

#include <algorithm>
#include <cmath>

#include "Constants.h"
#include "PhysicsObject.h"

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace {
using ms::tick_units::accel;
using ms::tick_units::velocity;

// *Speed and *Acc values are already in px/s and px/s^2: gravityAcc 2000 and jumpSpeed 555
// give a 77.00625 px jump; both were reproduced by the original-client capture. walkSpeed 125
// is a coefficient, not the player's baseline speed; get_walkspeed() multiplies it by
// SPEED / 100, so SPEED 100 gives 125 px/s.
//
// walkForce / walkDrag are ground-truthed by PHYSICS_FORCE_DIVISOR (100), measured on the
// original client: 1400 px/s^2 while a key is held, 800 px/s^2 once it is released. The other
// *Drag keys still use tick_units' 0.014 s calibration, which has no measured baseline.
constexpr double FIXED_POINT = 100000.0;

// Every field starts from the baseline archive's own keys, run through the same conversions
// load() applies to them. These are only reachable when the file or one of its keys is missing.
// swim_gravity is the one field with no key behind it and stays this client's own figure.
struct Params {
    double gravity = 2000.0;
    double max_fall_speed = 670.0;
    double jump_speed = 555.0;

    double walk_force = 140000.0;
    double walk_speed = 125.0;
    double walk_drag = 80000.0;

    double slip_force = 60000.0;
    double slip_speed = 120.0;

    double float_drag1 = 100000.0;
    double float_drag2 = 10000.0;
    double float_coefficient = 0.01;

    double max_friction = 2.0;
    double min_friction = 0.05;

    double fly_force = 120000.0;
    double fly_speed = 200.0;
    double fly_jump_dec = 0.35;

    double swim_force = 120000.0;
    double swim_speed = 140.0;
    double swim_speed_dec = 0.9;
    double swim_gravity = 468.75;
};

struct ParamField {
    const char *name;
    double Params::*member;
    bool fraction = false;
};

// Keep the archive-to-field mapping in one place. Speeds, forces, drag and friction are
// nonnegative; the two remaining-speed/jump fractions additionally lie in [0, 1].
constexpr ParamField PARAM_FIELDS[] = {
    {"gravityAcc", &Params::gravity},
    {"fallSpeed", &Params::max_fall_speed},
    {"jumpSpeed", &Params::jump_speed},
    {"walkForce", &Params::walk_force},
    {"walkDrag", &Params::walk_drag},
    {"walkSpeed", &Params::walk_speed},
    {"slipForce", &Params::slip_force},
    {"slipSpeed", &Params::slip_speed},
    {"minFriction", &Params::min_friction},
    {"maxFriction", &Params::max_friction},
    {"floatDrag1", &Params::float_drag1},
    {"floatDrag2", &Params::float_drag2},
    {"floatCoefficient", &Params::float_coefficient},
    {"flyForce", &Params::fly_force},
    {"flySpeed", &Params::fly_speed},
    {"flyJumpDec", &Params::fly_jump_dec, true},
    {"swimForce", &Params::swim_force},
    {"swimSpeed", &Params::swim_speed},
    {"swimSpeedDec", &Params::swim_speed_dec, true},
};

Params params;

bool numeric(const img::node &src, const char *name, double &out) {
    const img::node value = src[name];

    if (!value) {
        return false;
    }

    const img::node::type type = value.data_type();

    if (type != img::node::type::integer && type != img::node::type::real) {
        return false;
    }

    out = value.get_real();
    return std::isfinite(out);
}
}  // namespace

namespace ms {
void PhysicsParams::load() {
    // Loading a different/incomplete archive must not inherit values from the previous one.
    params = Params{};
    const img::node src = img::wz::map["Physics.img"];

    if (!src) {
#ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_INFO,
                            "PhysicsParams",
                            "Map/Physics.img absent, keeping built-in values");
#endif
        return;
    }

    double raw;
    for (const ParamField &field : PARAM_FIELDS) {
        if (numeric(src, field.name, raw) && raw >= 0.0
            && (!field.fraction || raw <= 1.0)) {
            params.*field.member = raw;
        }
    }

#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO,
                        "PhysicsParams",
                        "tick %.5f  gravity %.1f  fall %.1f  jump %.1f  walk %.1f accel %.1f friction %.1f  ice %.1f/%.1f@%.3f  float %.1f/%.4f  fly %.1f/%.1f@%.2f/%.2f  swim %.1f/%.1f@%.2f  swimg %.1f",
                        Constants::TICK_SECONDS,
                        params.gravity,
                        params.max_fall_speed,
                        params.jump_speed,
                        params.walk_speed,
                        ground_accel(),
                        ground_friction(),
                        slip_speed(),
                        slip_accel(),
                        ice_damping(min_friction()),
                        float_drag1(),
                        params.float_coefficient,
                        fly_speed(),
                        fly_accel(),
                        fly_damping(),
                        fly_jump_dec(),
                        swim_speed(),
                        swim_accel(),
                        swim_damping(),
                        swim_gravity());
#endif
}

double PhysicsParams::gravity() {
    return params.gravity;
}

double PhysicsParams::max_fall_speed() {
    return params.max_fall_speed;
}

double PhysicsParams::jump_speed() {
    return params.jump_speed;
}

double PhysicsParams::walk_speed() {
    return params.walk_speed;
}

double PhysicsParams::walk_accel() {
    return accel(params.walk_force / FIXED_POINT);
}

double PhysicsParams::ground_accel() {
    // Measured: vx += 42 px per 30 ms tick = 1400 px/s^2 = walkForce / 100.
    return params.walk_force / Constants::PHYSICS_FORCE_DIVISOR;
}

double PhysicsParams::ground_friction() {
    // Measured: vx -= 24 px per 30 ms tick = 800 px/s^2 = walkDrag / 100, applied toward
    // zero and clamped there so a released walk slides ~9.4 px in ~180 ms.
    return params.walk_drag / Constants::PHYSICS_FORCE_DIVISOR;
}

double PhysicsParams::ground_drive(double target_speed) {
    if (params.walk_speed <= 0.0) {
        return 0.0;
    }

    // A constant acceleration scaled to this entity's own top speed. move_normal divides it
    // back by ground_accel() to recover target_speed as the hard cap, so players with SPEED
    // stats, mobs and pets all keep the speeds they had without another per-object field.
    return ground_accel() * target_speed / params.walk_speed;
}

double PhysicsParams::min_friction() {
    return params.min_friction;
}

double PhysicsParams::max_friction() {
    return params.max_friction;
}

double PhysicsParams::clamp_friction(double friction) {
    const double lo = std::min(params.min_friction, params.max_friction);
    const double hi = std::max(params.min_friction, params.max_friction);

    return std::clamp(friction, lo, hi);
}

double PhysicsParams::slip_accel() {
    return accel(params.slip_force / FIXED_POINT);
}

double PhysicsParams::slip_speed() {
    return params.slip_speed;
}

double PhysicsParams::ice_damping(double friction) {
    if (params.slip_speed <= 0.0) {
        return 0.0;
    }

    // Legacy model, no longer called by move_normal. Kept so the archive conversions stay
    // inspectable until an ice baseline is captured.
    return slip_accel() / params.slip_speed * clamp_friction(friction);
}

double PhysicsParams::float_drag1() {
    return velocity(params.float_drag1 / FIXED_POINT);
}

double PhysicsParams::float_drag2() {
    return velocity(params.float_drag2 / FIXED_POINT);
}

double PhysicsParams::float_buoyancy() {
    return params.float_coefficient * params.gravity;
}

double PhysicsParams::fly_accel() {
    return accel(params.fly_force / FIXED_POINT);
}

double PhysicsParams::fly_speed() {
    return params.fly_speed;
}

double PhysicsParams::fly_damping() {
    return float_drag2();
}

double PhysicsParams::fly_jump_dec() {
    return params.fly_jump_dec;
}

double PhysicsParams::swim_accel() {
    return accel(params.swim_force / FIXED_POINT);
}

double PhysicsParams::swim_speed() {
    return params.swim_speed;
}

double PhysicsParams::swim_damping() {
    return velocity(1.0 - params.swim_speed_dec);
}

double PhysicsParams::swim_gravity() {
    return params.swim_gravity;
}
}  // namespace ms
