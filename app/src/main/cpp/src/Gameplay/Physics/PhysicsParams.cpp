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
using ms::Constants::GROUND_FRICTION;
using ms::tick_units::accel;
using ms::tick_units::velocity;

// The archive mixes two generations of units. Every *Speed and *Acc key is already in pixels per
// second (or per second squared), which is the unit Params holds its fields in: gravityAcc 2000 and
// jumpSpeed 555 put the apex of a jump at 77 pixels, and walkSpeed 125 is exactly walkForce over
// walkDrag. The *Force and *Drag keys are the other generation - a fixed-point fraction of a pixel,
// per tick of the 14 ms integrator this archive was written for.
//
// The loop runs at that same tick, so nothing has to be rescaled between two cadences: a *Force is
// already so many pixels per tick squared and a *Drag already the fraction of its speed a tick takes
// off, and both leave the fixed point and go through tick_units like every other per-tick figure in
// this client. A drag is dimensionless rather than a speed, but per-tick to per-second is the same
// division either way.
//
// That the second family reproduces the first is the proof that 14 ms is the archive's tick: 1.4 over
// 0.8 is 1.75 pixels per tick, and 125 pixels per second divided by 1.75 is 71.43, which is exactly
// 1/0.014.
constexpr double FIXED_POINT = 100000.0;

// Where every field starts from: the baseline archive's own keys, run through the same conversions
// load() applies to them, so that a default and the number it stands for cannot drift apart. They
// are only reachable when the file or one of its keys is missing, and keeping them as archive
// values rather than as this client's old per-tick figures means the absent case continues the
// physics that is actually running instead of reaching back for one nobody has tested.
//
// swim_gravity is the one field the archive has no key for, so it stays this client's own figure:
// 468.75 pixels a second squared, which is the three hundredths of a pixel per tick squared it was
// written at, held against the same damping so that a body settles at the sinking speed it always did.
//
// walk_accel is the other that is not the archive's own: 3125 is Constants::GROUND_FRICTION over the
// tick, times the archive's walkSpeed, which is the drive that settles at 125 under the damping this
// client actually uses.
struct Params {
    double gravity = 2000.0;
    double max_fall_speed = 670.0;
    double jump_speed = 555.0;
    double walk_accel = 3125.0;
    double walk_speed = 125.0;
    double ground_damping = velocity(GROUND_FRICTION);
    double fly_accel = accel(120000.0 / FIXED_POINT);
    double fly_speed = 200.0;
    double fly_damping = velocity(10000.0 / FIXED_POINT);
    double swim_accel = accel(120000.0 / FIXED_POINT);
    double swim_speed = 140.0;
    double swim_damping = velocity(1.0 - 0.9);
    double swim_gravity = 468.75;
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

    // walkSpeed is the speed a walk is held to, and it is the figure the eye checks - the ladder leap
    // names it outright too. walkForce and walkDrag are the pair the archive uses to settle at that
    // same speed, and neither is read: their ratio is the ramp, and the ramp they give is a tick up to
    // full speed and three to a dead stop. The drive comes from the speed instead, against the damping
    // this client actually runs, so that the walking speed stays the archive's and only the time it
    // takes to get there is this client's. See Constants::GROUND_FRICTION.
    if (numeric(src, "walkSpeed", raw)) {
        params.walk_speed = raw;
    }

    params.ground_damping = velocity(Constants::GROUND_FRICTION);
    params.walk_accel = ground_drive(params.walk_speed);

    if (numeric(src, "flyForce", raw)) {
        params.fly_accel = accel(raw / FIXED_POINT);
    }

    if (numeric(src, "flySpeed", raw)) {
        params.fly_speed = raw;
    }

    // floatDrag2 is the flight drag, and the archive says so twice over. swimSpeedDec 0.9 names the
    // water's drag as the fraction left after a tick, so the water takes 0.1 off; floatDrag2 is
    // 10000 over 100000, the same 0.1, in the fixed-point notation the *Drag keys use. The two
    // states the archive gives equal force (flyForce and swimForce are both 120000) therefore come
    // out with equal drag, and each is then bounded by the speed the archive names for it - which
    // is the only reading under which flySpeed 200 does any work, since flyForce over the stronger
    // floatDrag1 settles at 86 and would never reach the cap.
    if (numeric(src, "floatDrag2", raw)) {
        params.fly_damping = velocity(raw / FIXED_POINT);
    }

    if (numeric(src, "swimForce", raw)) {
        params.swim_accel = accel(raw / FIXED_POINT);
    }

    if (numeric(src, "swimSpeed", raw)) {
        params.swim_speed = raw;
    }

    // swimSpeedDec is the water's drag written the other way round: it names how much of the speed
    // is left after a tick, not how much is taken off.
    if (numeric(src, "swimSpeedDec", raw)) {
        params.swim_damping = velocity(1.0 - raw);
    }

    // The keys left over either name a state this client does not have or belong to the walk that is
    // now driven from walkSpeed, so reading them would only store a number nothing looks at:
    //
    // walkForce and walkDrag, with minFriction and maxFriction bounding that drag, are the pair above.
    //
    // slipForce and slipSpeed are the drive and the cap for ice, and PhysicsObject::Type::ICE is
    // never selected - no foothold carries a slippery flag, because Foothold only parses
    // id/prev/next/layer and its two endpoints.
    //
    // floatDrag1 and floatCoefficient belong to a body floating at the surface of the water, which
    // PlayerFlyState never chooses: underwater it takes SWIMMING and above it FLYING.
    //
    // flyJumpDec is what a jump costs you while flying, and PlayerFlyState binds no jump key.

#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO,
                        "PhysicsParams",
                        "tick %.5f  gravity %.1f  fall %.1f  jump %.1f  walk %.1f/%.1f@%.2f  fly %.1f/%.1f@%.2f  swim %.1f/%.1f@%.2f  swimg %.1f",
                        Constants::TICK_SECONDS,
                        params.gravity,
                        params.max_fall_speed,
                        params.jump_speed,
                        params.walk_speed,
                        params.walk_accel,
                        params.ground_damping,
                        params.fly_speed,
                        params.fly_accel,
                        params.fly_damping,
                        params.swim_speed,
                        params.swim_accel,
                        params.swim_damping,
                        params.swim_gravity);
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
