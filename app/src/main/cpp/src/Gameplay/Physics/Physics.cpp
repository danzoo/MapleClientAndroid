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

#include <cmath>

#include "PhysicsParams.h"
#include "../../Constants.h"

namespace ms {
// Ground takes speed off an object in two parts: walkDrag, which is proportional to the speed it
// already has, and this slope term, which is proportional to the square of it and to how steep the
// foothold is. That the second one is quadratic is why it keeps its shape untouched by the change of
// time unit - a factor of seconds squared is exactly what a squared speed gives you.
const double SLOPEFACTOR = 0.1;
const double GROUNDSLIP = 3.0;

// A slope this steep is treated as this steep, whatever the foothold says.
const double MAXSLOPE = 0.5;

// Nothing that is not being pushed keeps going this slowly: below here an object is considered to
// have stopped. The archive's minFriction does the same job where a map sets its own friction, and
// Constants::STOP_SPEED is the client's own restatement for when it does not.

// Longest distance a collision check is asked to cover on its own, in pixels. Anything that would
// move further than this in one update does it in pieces with a check between each - see
// move_object(). It is roughly the thickness of the thinnest foothold a map is drawn with, which is
// what an object has to clear between two checks to end up on the far side of it without touching.
const double MAX_SUBSTEP = 4.0;

// Free fall, the fastest a fall can go, the walk and the swim all come from Map/Physics.img now.
// Gravity is added to the downward speed every tick with nothing taking it back off, so the cap is
// what stops a long drop from crossing the screen in a handful of frames. It only binds past
// v*v/2g, roughly a hundred pixels down, so platform-height drops and jumps still fall exactly as
// they always have.

Physics::Physics(const img::node &src) {
    fh_tree_ = src;
}

Physics::Physics() = default;

void Physics::move_object(PhysicsObject &phobj) const {
    // Determine which platform the object is currently on
    fh_tree_.update_fh(phobj);

    // Use the appropriate physics for the terrain the object is on
    switch (phobj.type) {
        case PhysicsObject::Type::NORMAL:
            move_normal(phobj);
            break;
        case PhysicsObject::Type::FLYING:
            move_flying(phobj);
            break;
        case PhysicsObject::Type::SWIMMING:
            move_swimming(phobj);
            break;
        case PhysicsObject::Type::FIXATED:
        default: break;
    }

    // The tick's displacement, walked in pieces of at most MAX_SUBSTEP pixels with a collision check
    // between them, so that an object covering more ground than a platform is thick cannot be
    // between two checks' worth of positions on either side of it. Whatever the checks leave of the
    // speeds is then applied a piece at a time.
    //
    // Nothing slower than MAX_SUBSTEP pixels per tick takes more than one piece, and one piece is
    // asked about with the same whole-tick displacement the single check used to be made with, so
    // walking, jumping and every other ordinary movement come out of this exactly as they came out
    // of the old one-step code. Only an object that would otherwise cross several footholds between
    // two checks is affected - at the archive's terminal fall speed that is nine pixels a tick.
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
}

void Physics::move_normal(PhysicsObject &phobj) const {
    phobj.vacc = 0.0;
    phobj.hacc = 0.0;

    if (phobj.onground) {
        phobj.vacc += phobj.vforce;
        phobj.hacc += phobj.hforce;

        if (phobj.hacc == 0.0 && std::abs(phobj.hspeed) < Constants::STOP_SPEED) {
            phobj.hspeed = 0.0;
        } else {
            double slopef = phobj.fhslope;

            if (slopef > MAXSLOPE) {
                slopef = MAXSLOPE;
            } else if (slopef < -MAXSLOPE) {
                slopef = -MAXSLOPE;
            }

            phobj.hacc -= PhysicsParams::ground_damping() * phobj.hspeed;
            phobj.hacc += SLOPEFACTOR * slopef * phobj.hspeed * phobj.hspeed
                / (GROUNDSLIP * GROUNDSLIP);
        }
    } else if (phobj.is_flag_not_set(PhysicsObject::Flag::NO_GRAVITY)) {
        phobj.vacc += PhysicsParams::gravity();
    }

    phobj.hforce = 0.0;
    phobj.vforce = 0.0;

    phobj.hspeed += phobj.hacc * Constants::TICK_SECONDS;
    phobj.vspeed += phobj.vacc * Constants::TICK_SECONDS;

    const double maxfall = PhysicsParams::max_fall_speed();

    if (!phobj.onground && phobj.vspeed > maxfall) {
        phobj.vspeed = maxfall;
    }
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