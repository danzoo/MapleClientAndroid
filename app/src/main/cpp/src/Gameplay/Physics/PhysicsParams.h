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
// Movement tunables, taken from Map/Physics.img at startup.
//
// Values use px/s for speeds, px/s^2 for accelerations and 1/s for damping. *Speed and *Acc
// already use those units; legacy fixed-point *Force and *Drag use PHYSICS_REFERENCE_SECONDS
// for conversion, independently of the simulation timestep. walk_speed() is the coefficient
// used with (SPEED + 20) / 100, not the player's baseline walking speed.
//
// A missing archive, or a missing key inside it, leaves that one value at what this client was
// built with, so a Data tree that predates the file still plays.
class PhysicsParams {
public:
    // Read the archive. Has to run after the img tree has been mounted.
    static void load();

    // Free fall acceleration.
    static double gravity();

    // Fastest a fall can go.
    static double max_fall_speed();

    // The speed a jump leaves the ground with.
    static double jump_speed();

    // The walking-speed coefficient, before the player's stat multiplier.
    static double walk_speed();

    // How fast ground sheds speed, as a fraction of the current speed per second. This is what
    // decides how far anything that has been knocked, jumped off or let go of slides.
    static double ground_damping();

    // Acceleration required to hold target_speed under the client's ground damping.
    // Shared by players, mobs and pets.
    static double ground_drive(double target_speed);

    // The force that drives flight, and the speed flight settles at.
    static double fly_accel();

    static double fly_speed();

    // Flight drag, from the archive's floatDrag2.
    static double fly_damping();

    // The force that drives a stroke, and the speed swimming settles at.
    static double swim_accel();

    static double swim_speed();

    // Water drag, from the archive's swimSpeedDec.
    static double swim_damping();

    // How fast water pulls a swimmer down. The one live figure with no key behind it: the
    // archive's floatCoefficient is the only candidate, and reading it as a fraction of gravityAcc
    // would take this from 468.8 to 20 pixels per second squared, which is a change of behaviour
    // rather than of units, so it stays this client's own SWIMGRAVFORCE until that is asked for.
    static double swim_gravity();
};
}  // namespace ms
