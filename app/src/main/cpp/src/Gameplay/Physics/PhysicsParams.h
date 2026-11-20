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
#pragma once

namespace ms {
// Movement tunables, taken from Map/Physics.img at startup. All 19 keys in that file are read.
//
// Values use px/s for speeds and px/s^2 for accelerations. *Speed and *Acc already use those
// units directly and were confirmed against a per-frame capture of the original PC client.
// Legacy fixed-point walkForce / walkDrag are ground-truthed by PHYSICS_FORCE_DIVISOR (100),
// also measured there: walkForce/100 = 1400 px/s^2 of acceleration, walkDrag/100 = 800 px/s^2
// of friction, ratio 1.75 = walkForce : walkDrag. The other *Drag keys still use
// PHYSICS_REFERENCE_SECONDS, a calibration kept only for the flight / swim / floating models,
// which have not been measured. walk_speed() is the coefficient used with SPEED / 100, not the
// player's baseline walking speed.
//
// Each load starts from baseline defaults. Missing, nonnumeric, nonfinite or negative values
// retain their defaults; remaining-speed/jump fractions must also be in [0, 1]. A partial or
// older Data tree therefore cannot inherit values from a previously mounted archive.
class PhysicsParams {
public:
    // Reset defaults and read the archive after the img tree has been mounted.
    static void load();

    // Free fall acceleration.
    static double gravity();

    // Fastest a fall can go.
    static double max_fall_speed();

    // The speed a jump leaves the ground with.
    static double jump_speed();

    // The walking-speed coefficient, before the player's stat multiplier.
    static double walk_speed();

    // CONSTANT acceleration while a walk key is held, px/s^2: walkForce / 100 = 1400.
    // Measured on the original PC client (evidence/baseline/physics_baseline.json).
    static double ground_accel();

    // CONSTANT friction once the key is released, px/s^2: walkDrag / 100 = 800.
    // Applied toward zero and clamped there, never overshooting.
    static double ground_friction();

    // ground_accel() scaled for an entity whose target speed differs from walkSpeed (players
    // with SPEED stats, mobs, pets). move_normal inverts this relation to recover that target
    // as the hard speed cap, so every entity keeps its own top speed.
    static double ground_drive(double target_speed);

    // Raw archive drive under the legacy tick_units conversion, in px/s^2. Kept for the
    // unmeasured models and diagnostics; ground motion does not use it.
    static double walk_accel();

    // ---- Ice / slippery footholds -------------------------------------------------
    // Bounds on a foothold's friction coefficient. Ordinary ground is 1.0; the archive's
    // minFriction is used on footholds that carry a `force` (conveyor/slide) property.
    static double min_friction();

    static double max_friction();

    static double clamp_friction(double friction);

    // Legacy velocity-proportional ice damping, retained for diagnostics only. ICE now uses the
    // measured constant-acceleration model with clamp_friction() scaling both constants; no
    // ice baseline has been captured yet, so that scaling is an inference, not a measurement.
    static double ice_damping(double friction);

    // The archive's speed cap for the slipping state.
    static double slip_speed();

    // Raw archive slip drive, in px/s^2.
    static double slip_accel();

    // ---- Floating / water surface --------------------------------------------------
    // Horizontal drag of a body at the water surface, from floatDrag1 (1/s).
    static double float_drag1();

    // Vertical drag, from floatDrag2 (1/s). Also the flight drag.
    static double float_drag2();

    // floatCoefficient read as a fraction of gravity: the buoyancy spring constant for a
    // body displaced from the waterline, in 1/s^2.
    static double float_buoyancy();

    // ---- Air steering (NORMAL / ICE) ---------------------------------------------
    // Recovered from 0x9B2EA9 -> 0x9B2BCB in the original PC client. While a direction
    // is held in the air the client calls its velocity integrator with
    // force = +/- 2 * floatDrag2 * t2, mass = 100 and
    // limit = walkSpeed / walkForce * floatDrag2 * t2. t2 is the runtime wrapper
    // value measured as 1.0, so these are 200 px/s^2 and 8.928571 px/s.
    // The integrator early-exits when the current speed is already beyond the
    // limit in the force's direction, which is why a capped running jump is
    // unchanged while an opposing steer loses 6 px/s per 30 ms tick.
    static double air_steer_accel();

    static double air_steer_limit();

    // ---- Flight --------------------------------------------------------------------
    // The force that drives flight, and the speed flight settles at.
    static double fly_accel();

    static double fly_speed();

    // Flight drag, from the archive's floatDrag2.
    static double fly_damping();

    // What a jump costs while flying: the fraction of a ground jump impulse left over.
    static double fly_jump_dec();

    // ---- Swimming ------------------------------------------------------------------
    // The force that drives a stroke, and the speed swimming settles at.
    static double swim_accel();

    static double swim_speed();

    // Water drag, from the archive's swimSpeedDec.
    static double swim_damping();

    // How fast water pulls a swimmer down. The one live figure with no key behind it: the
    // archive's floatCoefficient is now read as buoyancy for FLOATING, and reading it as a
    // fraction of gravityAcc here would take this from 468.8 to 20 pixels per second squared,
    // which is a change of behaviour rather than of units, so it stays this client's own
    // SWIMGRAVFORCE for fully submerged bodies.
    static double swim_gravity();
};
}  // namespace ms
