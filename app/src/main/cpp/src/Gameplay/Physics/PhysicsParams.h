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
// Everything handed out here is in seconds: pixels per second for a speed, pixels per second
// squared for an acceleration, per second for a damping rate. That is the unit the archive's own
// *Speed and *Acc keys are already written in, and the keys that are not - the *Force and *Drag
// fixed-point pairs, which are quoted per tick of the 14 ms integrator the archive was written for -
// only have to leave the fixed point behind, because Constants::TIMESTEP is that same 14 ms.
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

    // The force that drives a walk.
    static double walk_accel();

    // The speed a walk settles at.
    static double walk_speed();

    // How fast ground sheds speed, as a fraction of the current speed per second. This is what
    // decides how far anything that has been knocked, jumped off or let go of slides.
    static double ground_damping();

    // The force that, once ground_damping() has taken as much off it as it adds, holds a given
    // walking speed. Anything that walks under the archive's damping but is not a player - a mob,
    // a pet - states its drive with this instead of naming an acceleration.
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
