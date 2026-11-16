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
#include "Physics.h"

#include <algorithm>
#include <cmath>

#include "PhysicsParams.h"
#include "../../Constants.h"

namespace ms {
// Nothing that is not being pushed keeps going this slowly: below here an object is considered to
// have stopped. The archive's minFriction does the same job where a map sets its own friction, and
// Constants::STOP_SPEED is the client's own restatement for when it does not.

// Longest distance a collision check is asked to cover on its own, in pixels. Anything that would
// move further than this in one update does it in pieces with a check between each - see
// move_object(). It is roughly the thickness of the thinnest foothold a map is drawn with, which is
// what an object has to clear between two checks to end up on the far side of it without touching.
const double MAX_SUBSTEP = 4.0;

// Free fall, the fastest a fall can go, the walk, the ice, the float and the swim all come from
// Map/Physics.img now. Gravity is added to the downward speed every tick with nothing taking it
// back off, so the cap is what stops a long drop from crossing the screen in a handful of frames.
// It only binds past v*v/2g, roughly a hundred pixels down, so platform-height drops and jumps
// still fall exactly as they always have.

Physics::Physics(const img::node &src) {
    fh_tree_ = src;
}

Physics::Physics() = default;

void Physics::move_object(PhysicsObject &phobj) const {
    // Capture the landing inputs before update_fh and move_normal consume them.
    // onground at entry is the previous tick's settled state, so this is the
    // false -> true edge the original uses for its one-shot landing conversion.
    // hforce carries the held direction sign and move_normal clears it below.
    phobj.was_airborne = !phobj.onground;
    phobj.landing_input_dir = phobj.hforce > 0.0 ? 1.0
                              : phobj.hforce < 0.0 ? -1.0 : 0.0;
    phobj.landing_conversion = false;

    // Determine which platform the object is currently on
    fh_tree_.update_fh(phobj);

    // Normal motion uses the mean vertical speed for collision travel, while the next tick
    // starts with the end speed. Keep the other movement engines unchanged.
    // NORMAL and ICE share one integrator and one collision path; both need the mean vertical
    // speed saved for travel and the end-of-step speed restored afterwards.
    const bool normal = phobj.is_ground_type();
    double end_vspeed = 0.0;
    phobj.vertical_limited = false;
    switch (phobj.type) {
        case PhysicsObject::Type::NORMAL:
        case PhysicsObject::Type::ICE:
            end_vspeed = move_normal(phobj);
            break;
        case PhysicsObject::Type::FLYING:
            move_flying(phobj);
            break;
        case PhysicsObject::Type::SWIMMING:
            move_swimming(phobj);
            break;
        case PhysicsObject::Type::FLOATING:
            move_floating(phobj);
            break;
        case PhysicsObject::Type::FIXATED:
        default: break;
    }

    // The tick's displacement, walked in pieces of at most MAX_SUBSTEP pixels with a collision check
    // between them, so that an object covering more ground than a platform is thick cannot be
    // between two checks' worth of positions on either side of it. Whatever the checks leave of the
    // speeds is then applied a piece at a time.
    //
    // Normal vertical travel is v0 * dt + 0.5 * gravity * dt^2, including the take-off step.
    // The piecewise collision path still approximates that arc with short line segments; dividing
    // it into pieces does not apply gravity or the take-off impulse more than once per tick.
    //
    // normalize() first, then shift(): the update still reads as one segment from where the object
    // started to where it ends up, which is what the drawing interpolates along. Advancing that
    // segment once per piece instead would make a fast object stutter between the pieces it is drawn
    // across.
    phobj.normalize();

    double hrem = phobj.hstep();
    double vrem = phobj.vstep();

    while (hrem != 0.0 || vrem != 0.0) {
        double reach = std::max(std::abs(hrem), std::abs(vrem));
        double share = reach > MAX_SUBSTEP ? MAX_SUBSTEP / reach : 1.0;
        double dh = hrem * share;
        double dv = vrem * share;

        // A fixated body is never asked about against the terrain, which is what the reference does
        // too - it limits movement only for normal, flying and swimming bodies - and it has to be
        // that way: a climber keeps the foothold it grabbed from, because update_fh returns early for
        // a fixated object, so the floor test would measure it against that very platform and clamp
        // every downward piece back onto it. Descent would be impossible.
        if (phobj.type != PhysicsObject::Type::FIXATED) {
            fh_tree_.limit_movement(phobj, dh, dv);
        }

        // A piece that ran into something is already over: the check put the object against the
        // obstacle it hit and took that direction's speed away, so the rest of the tick has nothing
        // left to cover the way it was going.
        bool hstopped = hrem != 0.0 && !phobj.hmobile();
        bool vstopped = vrem != 0.0 && !phobj.vmobile();

        phobj.apply_substep(hstopped ? 0.0 : dh, vstopped ? 0.0 : dv);

        hrem = hstopped ? 0.0 : hrem - dh;
        vrem = vstopped ? 0.0 : vrem - dv;

        // Whichever platform the tick started on is not necessarily the one under the object now,
        // and a piece is only worth taking if it is measured against the ground actually below it.
        if (hrem != 0.0 || vrem != 0.0) {
            fh_tree_.update_fh(phobj);
        }
    }

    // ---- Landing conversion (one-shot, input-priority) --------------------
    // This is the standing-jump-on-a-slope behaviour: with no direction key
    // held, the body keeps HALF of the impact velocity's tangential projection
    // and slides down the hill (REPORT.md 5.9). With vy positive down and the
    // +x unit tangent (c, s):
    //
    //     v = 0.5 * c * (vx_air * c + vy_air * s)
    //
    // A continuously held direction suppresses the whole conversion (see the
    // capture gate in FootholdTree.cpp): the carried horizontal speed must
    // survive a running jump, otherwise every touchdown costs momentum and an
    // uphill jump gets clamped to zero at the foot of the slope.
    //
    // The exception is a steer that CHANGED while airborne. That landing uses
    // the original projection and its opposite-input clamp so the body stops
    // the old momentum and the new direction takes over immediately, while
    // the auto-jump is cancelled (PlayerFallState). Jump and movement are
    // isolated by the input state, per the user's 2026-11-16 specification;
    // this is explicitly not a PC-measured constant.
    if (phobj.landing_conversion) {
        const double c = phobj.landing_fhcos;
        const double s = phobj.landing_fhsin;
        const double v_air_x = phobj.landing_vx;
        const double v_air_y = phobj.landing_vy;
        double v_new = 0.5 * c * (v_air_x * c + v_air_y * s);

        if (phobj.landing_input_dir != 0.0
            && v_new * phobj.landing_input_dir < 0.0) {
            v_new = 0.0;
        }

        // The original integrates position with (v_old + v_new) / 2 * dt. The
        // airborne part of this tick already travelled at v_air_x for the whole
        // step, so only half of the velocity change belongs to the landing step.
        phobj.x.shift(0.5 * (v_new - v_air_x) * Constants::TICK_SECONDS);
        phobj.hspeed = v_new;

        const Foothold &landing = fh_tree_.get_foothold(phobj.landing_fhid);
        if (landing.id()) {
            phobj.fhid = phobj.landing_fhid;
            phobj.fhslope = landing.slope();
            phobj.fhcos = c;
            phobj.fhsin = s;
            // Re-seat the body on the surface at its corrected x; otherwise the
            // next update_fh would see y != ground and start a spurious fall.
            phobj.limity(landing.ground_below(phobj.crnt_x()));
            phobj.onground = true;
        }
        phobj.landing_conversion = false;
        // Keep air_direction_changed set through the landing tick: the player
        // state uses it AFTER physics to decide whether to re-arm the jump.
        // arm_jump() clears it when the next jump actually starts.
    }

    if (normal && !phobj.vertical_limited) {
        phobj.vspeed = end_vspeed;
    }
}

double Physics::move_normal(PhysicsObject &phobj) const {
    const double dt = Constants::TICK_SECONDS;
    double start_vspeed = phobj.vspeed;
    phobj.vacc = 0.0;
    phobj.hacc = 0.0;

    // Ground motion is a direct translation of the original PC client's move update
    // (VA 0x9B2C3C flat, VA 0x9B23F2 / 0x9B1D3D slopes), validated against two
    // per-frame captures of the running client (physics_baseline.json and
    // slope.csv in F:\Rb\pys\evidence\baseline, REPORT.md 5.8 and 5.9):
    //
    //   key held:     hspeed += hforce * dt, then hard-clamp to +/- target_speed
    //   key released: hspeed -= sign(hspeed) * ground_friction() * dt, clamped at zero
    //
    // On a slope the client works along the foothold's UNIT TANGENT (c, s), not
    // in world x. Measured on the running client and confirmed in the disasm at
    // VA 0x9B262B..0x9B2642, the walk force is scaled by
    //     1 + s*|s|   (1 - s^2 for s<0, 1 + s^2 for s>=0, exactly 1 when flat)
    // and the top speed along the tangent is raised by 1 + s^2 when the input
    // points downhill. Friction stays a constant 800 px/s^2 ALONG the tangent,
    // so a standing body does not creep downhill and a landing slide stops.
    // With vx = speed_along * c this reproduces the sampled 2:3 slopes exactly:
    // 104.01 px/s world-x cap uphill, 136.01 downhill, 24.19 px/tick ramp on
    // the s<0 slope and 45.70 on the s>0 slope, 19.97 px/tick friction.
    const bool ice = phobj.type == PhysicsObject::Type::ICE;
    const double surface = ice ? PhysicsParams::clamp_friction(phobj.surface_friction) : 1.0;

    if (phobj.onground) {
        // vforce delivers a one-step take-off impulse, not an acceleration spread through
        // the flight. Gravity applies immediately after that impulse, including this step.
        start_vspeed += phobj.vforce * dt;

        const double base_accel = PhysicsParams::ground_accel();
        const double accel = base_accel * surface;
        const double decel = PhysicsParams::ground_friction() * surface;
        const double c = phobj.fhcos;
        const double s = phobj.fhsin;
        const double drive_factor = 1.0 + s * std::abs(s);
        const double sin2 = s * s;

        // Tangent speed. hforce is ground_drive(target) = ground_accel()*target/walkSpeed,
        // so dividing it back by ground_accel() recovers the entity's own top speed.
        double along = c > 0.0 ? phobj.hspeed / c : phobj.hspeed;

        if (phobj.hforce != 0.0) {
            const double dir = phobj.hforce > 0.0 ? 1.0 : -1.0;
            const double cap = (accel > 0.0
                ? std::abs(phobj.hforce) * PhysicsParams::walk_speed() / accel
                : PhysicsParams::walk_speed())
                * (dir * s > 0.0 ? 1.0 + sin2 : 1.0);
            const double along_acc = phobj.hforce * drive_factor;

            phobj.hacc = along_acc * c;
            along += along_acc * dt;
            along = std::clamp(along, -cap, cap);
        } else if (along != 0.0) {
            const double step = decel * dt;

            phobj.hacc = (along > 0.0 ? -decel : decel) * c;
            along = along > 0.0 ? std::max(0.0, along - step)
                                : std::min(0.0, along + step);
        }

        // A WZ foothold `force` is a signed conveyor/slide push, positive to the right.
        // It is applied whether or not a walk key is held. Maps in the captured baseline
        // have no `force` property, so its slope scaling has no dynamic baseline yet.
        if (phobj.foothold_force != 0.0) {
            along += phobj.foothold_force * dt;
            phobj.hacc += phobj.foothold_force * c;
        }

        if (ice) {
            const double maxslip = PhysicsParams::slip_speed();
            along = std::clamp(along, -maxslip, maxslip);
        }

        phobj.hspeed = along * c;
    }

    const bool airborne = !phobj.onground || start_vspeed < 0.0;
    if (airborne && phobj.is_flag_not_set(PhysicsObject::Flag::NO_GRAVITY)) {
        phobj.vacc = PhysicsParams::gravity();
    }

    phobj.hforce = 0.0;
    phobj.vforce = 0.0;

    const double maxfall = PhysicsParams::max_fall_speed();
    if (airborne) {
        start_vspeed = std::min(start_vspeed, maxfall);
    }

    double end_vspeed = start_vspeed + phobj.vacc * dt;
    double travel = (start_vspeed + end_vspeed) * 0.5 * dt;

    if (airborne && end_vspeed > maxfall) {
        // Integrate the accelerated portion only until terminal speed is reached, then the
        // rest at the cap. Averaging the capped end speed over the whole step would under-travel.
        const double accelerating = phobj.vacc > 0.0
            ? std::clamp((maxfall - start_vspeed) / phobj.vacc, 0.0, dt) : 0.0;
        travel = start_vspeed * accelerating
            + 0.5 * phobj.vacc * accelerating * accelerating
            + maxfall * (dt - accelerating);
        end_vspeed = maxfall;
    }

    // Collision code moves at a constant speed within its short pieces. Supplying the mean
    // speed gives the exact constant-gravity displacement; move_object restores the end speed.
    phobj.vspeed = travel / dt;
    return end_vspeed;
}

void Physics::move_flying(PhysicsObject &phobj) const {
    const double damping = PhysicsParams::fly_damping();

    phobj.hacc = phobj.hforce - damping * phobj.hspeed;
    phobj.vacc = phobj.vforce - damping * phobj.vspeed;
    phobj.hforce = 0.0;
    phobj.vforce = 0.0;

    phobj.hspeed += phobj.hacc * Constants::TICK_SECONDS;
    phobj.vspeed += phobj.vacc * Constants::TICK_SECONDS;

    // floatDrag2 leaves a flight settling far above the speed the archive names for flying, so it
    // is that named speed, not the drag, that bounds one.
    const double maxfly = PhysicsParams::fly_speed();

    if (phobj.hspeed > maxfly) {
        phobj.hspeed = maxfly;
    } else if (phobj.hspeed < -maxfly) {
        phobj.hspeed = -maxfly;
    }

    if (phobj.vspeed > maxfly) {
        phobj.vspeed = maxfly;
    } else if (phobj.vspeed < -maxfly) {
        phobj.vspeed = -maxfly;
    }

    if (phobj.hacc == 0.0 && std::abs(phobj.hspeed) < Constants::STOP_SPEED) {
        phobj.hspeed = 0.0;
    }

    if (phobj.vacc == 0.0 && std::abs(phobj.vspeed) < Constants::STOP_SPEED) {
        phobj.vspeed = 0.0;
    }
}

void Physics::move_swimming(PhysicsObject &phobj) const {
    const double damping = PhysicsParams::swim_damping();

    phobj.hacc = phobj.hforce - damping * phobj.hspeed;
    phobj.vacc = phobj.vforce - damping * phobj.vspeed;
    phobj.hforce = 0.0;
    phobj.vforce = 0.0;

    // The one pull in here that has no key behind it.
    if (phobj.is_flag_not_set(PhysicsObject::Flag::NO_GRAVITY)) {
        phobj.vacc += PhysicsParams::swim_gravity();
    }

    phobj.hspeed += phobj.hacc * Constants::TICK_SECONDS;
    phobj.vspeed += phobj.vacc * Constants::TICK_SECONDS;

    // The speed the archive names for swimming is the speed the strokes settle at, so it caps both
    // axes rather than only the direction being paddled towards.
    const double maxswim = PhysicsParams::swim_speed();

    if (phobj.hspeed > maxswim) {
        phobj.hspeed = maxswim;
    } else if (phobj.hspeed < -maxswim) {
        phobj.hspeed = -maxswim;
    }

    if (phobj.vspeed > maxswim) {
        phobj.vspeed = maxswim;
    } else if (phobj.vspeed < -maxswim) {
        phobj.vspeed = -maxswim;
    }

    if (phobj.hacc == 0.0 && std::abs(phobj.hspeed) < Constants::STOP_SPEED) {
        phobj.hspeed = 0.0;
    }

    if (phobj.vacc == 0.0 && std::abs(phobj.vspeed) < Constants::STOP_SPEED) {
        phobj.vspeed = 0.0;
    }
}

void Physics::move_floating(PhysicsObject &phobj) const {
    const double dt = Constants::TICK_SECONDS;

    // A body at the water surface is dragged hard sideways (floatDrag1) and gently up and
    // down (floatDrag2), with strokes still driven by swimForce and bounded by swimSpeed.
    phobj.hacc = phobj.hforce - PhysicsParams::float_drag1() * phobj.hspeed;
    phobj.vacc = phobj.vforce - PhysicsParams::float_drag2() * phobj.vspeed;
    phobj.hforce = 0.0;
    phobj.vforce = 0.0;

    // floatCoefficient is read as a fraction of gravity: a spring constant pulling the body
    // back to the map's waterline. +y is down, so a body above the line is pulled down and
    // one below it is pushed up. The restoring acceleration is capped at gravityAcc (the
    // displacement that would reach it is g / k), so falling in from high above still falls
    // like a normal body rather than being snapped to the surface. Bodies with no waterline
    // fall back to the submerged swim gravity.
    if (phobj.is_flag_not_set(PhysicsObject::Flag::NO_GRAVITY) && phobj.floating) {
        const double k = PhysicsParams::float_buoyancy();

        if (k > 0.0) {
            const double reach = PhysicsParams::gravity() / k;
            const double offset = std::clamp(phobj.waterline - phobj.crnt_y(), -reach, reach);
            phobj.vacc += k * offset;
        }
    } else if (phobj.is_flag_not_set(PhysicsObject::Flag::NO_GRAVITY)) {
        phobj.vacc += PhysicsParams::swim_gravity();
    }

    phobj.hspeed += phobj.hacc * dt;
    phobj.vspeed += phobj.vacc * dt;

    const double maxspeed = PhysicsParams::swim_speed();
    phobj.hspeed = std::clamp(phobj.hspeed, -maxspeed, maxspeed);
    phobj.vspeed = std::clamp(phobj.vspeed, -maxspeed, maxspeed);

    if (phobj.hacc == 0.0 && std::abs(phobj.hspeed) < Constants::STOP_SPEED) {
        phobj.hspeed = 0.0;
    }

    if (phobj.vacc == 0.0 && std::abs(phobj.vspeed) < Constants::STOP_SPEED) {
        phobj.vspeed = 0.0;
    }
}

Point<int16_t> Physics::get_y_below(Point<int16_t> position) const {
    int16_t ground = fh_tree_.get_y_below(position);

    return Point<int16_t>(position.x(), ground - 1);
}

Physics::Platform Physics::get_platform(uint16_t fhid) const {
    if (!fhid) {
        return {};
    }

    const Foothold &start = fh_tree_.get_foothold(fhid);
    Platform platform { start.l(), start.r() };

    // Follow the links this platform is joined to on both sides. Every step has to reach further
    // out than the last, which is what makes the walk end at the last platform of the stretch -
    // and at a wall, whose two horizontal components are the same place.
    for (uint16_t id = start.prev(); id; id = fh_tree_.get_foothold(id).prev()) {
        const Foothold &fh = fh_tree_.get_foothold(id);

        if (fh.l() >= platform.left) {
            break;
        }

        platform.left = fh.l();
    }

    for (uint16_t id = start.next(); id; id = fh_tree_.get_foothold(id).next()) {
        const Foothold &fh = fh_tree_.get_foothold(id);

        if (fh.r() <= platform.right) {
            break;
        }

        platform.right = fh.r();
    }

    return platform;
}

const FootholdTree &Physics::get_fht() const {
    return fh_tree_;
}
}  // namespace ms