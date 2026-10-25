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
using ms::tick_units::velocity;

// The archive mixes two generations of units. Every *Speed and *Acc key is already in pixels per
// second (or per second squared), which is what the whole table was eventually measured against:
// gravityAcc 2000 and jumpSpeed 555 put the apex of a jump at 77 pixels, and walkSpeed 125 is
// exactly walkForce over walkDrag. The *Force and *Drag keys are the other generation - a
// fixed-point fraction of a pixel, per tick of an integrator that advanced every 14 ms of real
// time - so those two have to be turned into an acceleration and a damping rate.
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
// damping rate. swimSpeedDec is the same thing written the other way round: it names how much is
// left, not how much is taken.
double gms_decay(double decayed_to) {
    return (1.0 - decayed_to) / GMS_TIMESTEP;
}

// What this client was built with, in the units it used, so that a Data tree without the archive
// still plays the same as it did. The ground damping is the one that Physics.cpp arrived at on its
// own: FRICTION plus SLOPEFACTOR, taken off an object in proportion to hspeed / GROUNDSLIP.
constexpr double LEGACY_GROUND_DAMPING = (0.3 + 0.1) / 3.0;
// A walk pushed with 0.16 and shed 13.33% of its speed per tick, so it settled at 1.2 pixels per
// tick; a jump started out at 4.5 and fell at 0.14; flight and swimming both pushed 0.25 against
// FLYFRICTION 0.05 and the old swim drag 0.08.
constexpr double LEGACY_WALK_ACCEL = 0.16;
constexpr double LEGACY_FLY_ACCEL = 0.25;
constexpr double LEGACY_SWIM_ACCEL = 0.25;
constexpr double LEGACY_JUMP_SPEED = 4.5;
constexpr double LEGACY_FALL_SPEED = 6.0;
constexpr double LEGACY_FLY_SPEED = 0.25 / 0.05;
constexpr double LEGACY_SWIM_SPEED = 0.25 / 0.08;

struct Params {
    double gravity = accel(ms::Constants::GRAVITY);
    double max_fall_speed = velocity(LEGACY_FALL_SPEED);
    double jump_speed = velocity(LEGACY_JUMP_SPEED);
    double walk_accel = accel(LEGACY_WALK_ACCEL);
    double walk_speed = velocity(LEGACY_WALK_ACCEL / LEGACY_GROUND_DAMPING);
    double ground_damping = LEGACY_GROUND_DAMPING / ms::Constants::LEGACY_TICK_SECONDS;
    double fly_accel = accel(LEGACY_FLY_ACCEL);
    double fly_speed = velocity(LEGACY_FLY_SPEED);
    double fly_damping = 0.05 / ms::Constants::LEGACY_TICK_SECONDS;
    double swim_accel = accel(LEGACY_SWIM_ACCEL);
    double swim_speed = velocity(LEGACY_SWIM_SPEED);
    double swim_damping = 0.08 / ms::Constants::LEGACY_TICK_SECONDS;
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

    double friction = LEGACY_GROUND_DAMPING;
    bool has_friction = numeric(src, "walkDrag", raw);

    if (has_friction) {
        friction = raw / FIXED_POINT;
    }

    // minFriction and maxFriction are the bounds the archive puts on that same coefficient, which
    // is why they read as 0.05 and 2.0 next to a walkDrag of 0.8.
    double min_friction, max_friction;

    if (numeric(src, "minFriction", min_friction)
        && friction < min_friction) {
        friction = min_friction;
    }

    if (numeric(src, "maxFriction", max_friction)
        && friction > max_friction) {
        friction = max_friction;
    }

    params.ground_damping = friction / GMS_TIMESTEP;

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
