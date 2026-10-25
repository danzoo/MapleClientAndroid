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

#include "Constants.h"
#include "PhysicsObject.h"

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace {
using ms::tick_units::accel;

// The archive mixes two generations of units. Every *Speed and *Acc key is already in pixels per
// second (or per second squared), which is the unit Params holds its fields in: gravityAcc 2000 and
// jumpSpeed 555 put the apex of a jump at 77 pixels, and walkSpeed 125 is exactly walkForce over
// walkDrag. The *Force and *Drag keys are the other generation - a fixed-point fraction of a pixel,
// per tick of an integrator that advanced every 14 ms of real time - so those two have to be turned
// into an acceleration and a damping rate.
//
// That the second family reproduces the first is the proof that 14 ms is the right tick: 1.4 over
// 0.8 is 1.75 pixels per tick, and 125 pixels per second divided by 1.75 is 71.43, which is
// exactly 1/0.014.
constexpr double GMS_TIMESTEP = 0.014;
constexpr double FIXED_POINT = 100000.0;

// A push that a 14 ms integrator was given every tick, restated as an acceleration.
double gms_accel(double fixed_point_force) {
    return fixed_point_force / FIXED_POINT / (GMS_TIMESTEP * GMS_TIMESTEP);
}

// The fraction of its speed that a 14 ms integrator took off an object every tick, restated as a
// damping rate.
double gms_damping(double taken_off) {
    return taken_off / GMS_TIMESTEP;
}

// swimSpeedDec is the same thing written the other way round: it names how much is left, not how
// much is taken.
double gms_decay(double decayed_to) {
    return gms_damping(1.0 - decayed_to);
}

// Where every field starts from: the baseline archive's own keys, run through the same conversions
// load() applies to them, so that a default and the number it stands for cannot drift apart. They
// are only reachable when the file or one of its keys is missing, and keeping them as archive
// values rather than as this client's old per-tick figures means the absent case continues the
// physics that is actually running instead of reaching back for one nobody has tested.
//
// fly_damping and swim_gravity are the two the archive has no key for, so those stay this client's
// own figures, quoted per timestep through the channel every other leftover per-tick number uses.
struct Params {
    double gravity = 2000.0;
    double max_fall_speed = 670.0;
    double jump_speed = 555.0;
    double walk_accel = gms_accel(140000.0);
    double walk_speed = 125.0;
    double ground_damping = gms_damping(80000.0 / FIXED_POINT);
    double fly_accel = gms_accel(120000.0);
    double fly_speed = 200.0;
    double fly_damping = 0.05 / ms::Constants::TICK_SECONDS;
    double swim_accel = gms_accel(120000.0);
    double swim_speed = 140.0;
    double swim_damping = gms_decay(0.9);
    double swim_gravity = accel(0.03);
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
    return true;
}
}  // namespace

namespace ms {
void PhysicsParams::load() {
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

    if (numeric(src, "gravityAcc", raw)) {
        params.gravity = raw;
    }

    if (numeric(src, "fallSpeed", raw)) {
        params.max_fall_speed = raw;
    }

    if (numeric(src, "jumpSpeed", raw)) {
        params.jump_speed = raw;
    }

    if (numeric(src, "walkForce", raw)) {
        params.walk_accel = gms_accel(raw);
    }

    // walkSpeed is the speed that walkForce and walkDrag already settle at, so it is only used
    // where a walk has to be named outright - the leap out onto a ladder, for instance.
    if (numeric(src, "walkSpeed", raw)) {
        params.walk_speed = raw;
    }

    // walkDrag is the ground friction coefficient the archive names, and minFriction and maxFriction
    // are the bounds it puts on that same coefficient - which is why they read as 0.05 and 2.0 next
    // to a walkDrag of 0.8. Without the key there is no coefficient to bound, so the default stands
    // instead of being run through a conversion meant for the archive's own figures.
    if (numeric(src, "walkDrag", raw)) {
        double friction = raw / FIXED_POINT;

        double min_friction, max_friction;

        if (numeric(src, "minFriction", min_friction)
            && friction < min_friction) {
            friction = min_friction;
        }

        if (numeric(src, "maxFriction", max_friction)
            && friction > max_friction) {
            friction = max_friction;
        }

        params.ground_damping = gms_damping(friction);
    }

    if (numeric(src, "flyForce", raw)) {
        params.fly_accel = gms_accel(raw);
    }

    if (numeric(src, "flySpeed", raw)) {
        params.fly_speed = raw;
    }

    if (numeric(src, "swimForce", raw)) {
        params.swim_accel = gms_accel(raw);
    }

    if (numeric(src, "swimSpeed", raw)) {
        params.swim_speed = raw;
    }

    if (numeric(src, "swimSpeedDec", raw)) {
        params.swim_damping = gms_decay(raw);
    }

    // slipForce and slipSpeed have nothing to bind to: this client has no ice physics -
    // PhysicsObject::Type::ICE is never selected and no foothold carries the flag that would ask
    // for it - and floatDrag1, floatDrag2, floatCoefficient and flyJumpDec describe a body
    // floating at the surface of the water, which is a state the player never enters either.

#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO,
                        "PhysicsParams",
                        "tick %.5f  gravity %.1f  fall %.1f  jump %.1f  walk %.1f/%.1f@%.2f  fly %.1f/%.1f  swim %.1f/%.1f@%.2f",
                        Constants::TICK_SECONDS,
                        params.gravity,
                        params.max_fall_speed,
                        params.jump_speed,
                        params.walk_speed,
                        params.walk_accel,
                        params.ground_damping,
                        params.fly_speed,
                        params.fly_accel,
                        params.swim_speed,
                        params.swim_accel,
                        params.swim_damping);
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

double PhysicsParams::jump_impulse() {
    // One tick of this is the whole speed the jump starts with.
    return params.jump_speed / Constants::TICK_SECONDS;
}

double PhysicsParams::walk_accel() {
    return params.walk_accel;
}

double PhysicsParams::walk_speed() {
    return params.walk_speed;
}

double PhysicsParams::ground_damping() {
    return params.ground_damping;
}

double PhysicsParams::ground_drive(double target_speed) {
    return params.ground_damping * target_speed;
}

double PhysicsParams::fly_accel() {
    return params.fly_accel;
}

double PhysicsParams::fly_speed() {
    return params.fly_speed;
}

double PhysicsParams::fly_damping() {
    return params.fly_damping;
}

double PhysicsParams::swim_accel() {
    return params.swim_accel;
}

double PhysicsParams::swim_speed() {
    return params.swim_speed;
}

double PhysicsParams::swim_damping() {
    return params.swim_damping;
}

double PhysicsParams::swim_gravity() {
    return params.swim_gravity;
}
}  // namespace ms
