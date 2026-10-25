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

namespace ms {
const double GRAVFORCE = Constants::GRAVITY;
const double SWIMGRAVFORCE = 0.03;
const double FRICTION = 0.3;
const double SLOPEFACTOR = 0.1;
const double GROUNDSLIP = 3.0;
const double FLYFRICTION = 0.05;
const double SWIMFRICTION = 0.08;

// Fastest a fall can go, in pixels per tick. Gravity is added to the downward speed every tick
// with nothing taking it back off, so a long drop keeps building up until the character crosses the
// screen in a handful of frames. This only binds past v*v/2g, about a hundred and twenty eight
// pixels down, so platform-height drops and jumps fall exactly as they always have.
const double MAXFALLSPEED = 6.0;

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
            fh_tree_.limit_movement(phobj);
            break;
        case PhysicsObject::Type::FLYING:
            move_flying(phobj);
            fh_tree_.limit_movement(phobj);
            break;
        case PhysicsObject::Type::SWIMMING:
            move_swimming(phobj);
            fh_tree_.limit_movement(phobj);
            break;
        case PhysicsObject::Type::FIXATED:
        default: break;
    }

    // Move the object forward
    phobj.move();
}

void Physics::move_normal(PhysicsObject &phobj) const {
    phobj.vacc = 0.0;
    phobj.hacc = 0.0;

    if (phobj.onground) {
        phobj.vacc += phobj.vforce;
        phobj.hacc += phobj.hforce;

        if (phobj.hacc == 0.0 && phobj.hspeed < 0.1 && phobj.hspeed > -0.1) {
            phobj.hspeed = 0.0;
        } else {
            double inertia = phobj.hspeed / GROUNDSLIP;
            double slopef = phobj.fhslope;

            if (slopef > 0.5) {
                slopef = 0.5;
            } else if (slopef < -0.5) {
                slopef = -0.5;
            }

            phobj.hacc -=
                (FRICTION + SLOPEFACTOR * (1.0 + slopef * -inertia)) * inertia;
        }
    } else if (phobj.is_flag_not_set(PhysicsObject::Flag::NO_GRAVITY)) {
        phobj.vacc += GRAVFORCE;
    }

    phobj.hforce = 0.0;
    phobj.vforce = 0.0;

    phobj.hspeed += phobj.hacc;
    phobj.vspeed += phobj.vacc;

    if (!phobj.onground && phobj.vspeed > MAXFALLSPEED) {
        phobj.vspeed = MAXFALLSPEED;
    }
}

void Physics::move_flying(PhysicsObject &phobj) const {
    phobj.hacc = phobj.hforce;
    phobj.vacc = phobj.vforce;
    phobj.hforce = 0.0;
    phobj.vforce = 0.0;

    phobj.hacc -= FLYFRICTION * phobj.hspeed;
    phobj.vacc -= FLYFRICTION * phobj.vspeed;

    phobj.hspeed += phobj.hacc;
    phobj.vspeed += phobj.vacc;

    if (phobj.hacc == 0.0 && phobj.hspeed < 0.1 && phobj.hspeed > -0.1) {
        phobj.hspeed = 0.0;
    }

    if (phobj.vacc == 0.0 && phobj.vspeed < 0.1 && phobj.vspeed > -0.1) {
        phobj.vspeed = 0.0;
    }
}

void Physics::move_swimming(PhysicsObject &phobj) const {
    phobj.hacc = phobj.hforce;
    phobj.vacc = phobj.vforce;
    phobj.hforce = 0.0;
    phobj.vforce = 0.0;

    phobj.hacc -= SWIMFRICTION * phobj.hspeed;
    phobj.vacc -= SWIMFRICTION * phobj.vspeed;

    if (phobj.is_flag_not_set(PhysicsObject::Flag::NO_GRAVITY)) {
        phobj.vacc += SWIMGRAVFORCE;
    }

    phobj.hspeed += phobj.hacc;
    phobj.vspeed += phobj.vacc;

    if (phobj.hacc == 0.0 && phobj.hspeed < 0.1 && phobj.hspeed > -0.1) {
        phobj.hspeed = 0.0;
    }

    if (phobj.vacc == 0.0 && phobj.vspeed < 0.1 && phobj.vspeed > -0.1) {
        phobj.vspeed = 0.0f;
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