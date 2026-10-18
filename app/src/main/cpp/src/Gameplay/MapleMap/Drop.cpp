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
#include "Drop.h"

#include <algorithm>
#include <cmath>

namespace ms {
namespace {
// Ticks an item spends in the air over level ground.
constexpr double DROP_TICKS = 76.0;

// How far above the corpse the item rises before it starts coming down. Kept away from the
// destination so an item that ends up on a platform below still gets the same toss up out of the
// mob, rather than having its arc flattened into a fall by the height it has left to drop.
constexpr double DROP_RISE = 42.0;

// Pixels per tick the fall stops accelerating at. Without it an item dropped from a high platform
// onto a low one covered the gap in a handful of ticks, which read as a snap rather than a fall.
constexpr double DROP_MAX_FALL = 3.0;

// The rise is half of the level-ground flight, and the same acceleration governs both halves of
// every flight so the arc is one curve. Falling the height of the rise takes exactly that long.
constexpr double RISE_TICKS = DROP_TICKS / 2.0;
constexpr double ACCEL = 2.0 * DROP_RISE / (RISE_TICKS * RISE_TICKS);

// Where the fall reaches DROP_MAX_FALL, and how far it has fallen by then.
constexpr double CAP_TICKS = DROP_MAX_FALL / ACCEL;
constexpr double CAP_DIST = 0.5 * ACCEL * CAP_TICKS * CAP_TICKS;

// Ticks spent falling from the peak through rise plus the height of the destination below it.
double fall_ticks(double distance) {
    if (distance <= CAP_DIST) {
        return std::sqrt(2.0 * distance / ACCEL);
    }

    return CAP_TICKS + (distance - CAP_DIST) / DROP_MAX_FALL;
}

// Height above the launch point after ticks of the rise, and distance below the peak after ticks
// of the fall.
double rise_offset(double ticks) {
    return ACCEL * RISE_TICKS * ticks - 0.5 * ACCEL * ticks * ticks;
}

double fall_offset(double ticks) {
    if (ticks <= CAP_TICKS) {
        return 0.5 * ACCEL * ticks * ticks;
    }

    return CAP_DIST + DROP_MAX_FALL * (ticks - CAP_TICKS);
}
}  // namespace

Drop::Drop(int32_t id,
           int32_t own,
           Point<int16_t> start,
           Point<int16_t> dst,
           int8_t type,
           int8_t mode,
           bool pldrp) :
    MapObject(id) {
    owner_ = own;
    set_position(start.x(), start.y() - 4);
    dest_ = dst;
    pickup_type_ = type;
    player_drop_ = pldrp;

    angle_.set(0.0f);
    opacity_.set(1.0f);
    moved_ = 0.0f;
    looter_ = nullptr;

    switch (mode) {
        case 0:
        case 1:
            state_ = Drop::State::DROPPED;
            base_y_ = static_cast<double>(dest_.y() - 4);
            // The walk to the destination is animated directly rather than handed to the terrain,
            // because a launch speed picked from the drop's height above the ground overshot the
            // spot the server measured the pickup from and the item got snapped back the moment it
            // touched down.
            launch_ = Point<int16_t>(start.x(), start.y() - 4);
            fallen_ = 0.0;

            // How far the destination sits below the corpse. Zero on level ground.
            air_ticks_ = RISE_TICKS + fall_ticks(
                DROP_RISE + std::max(0.0,
                    static_cast<double>((dest_.y() - 4) - launch_.y())));

            phobj_.type = PhysicsObject::Type::FIXATED;
            break;
        case 2:
            state_ = Drop::State::FLOATING;
            base_y_ = phobj_.crnt_y();
            phobj_.type = PhysicsObject::Type::FIXATED;
            break;
        case 3:
            state_ = Drop::State::PICKED_UP;
            phobj_.vspeed = -5.0f;
            break;
    }
}

int8_t Drop::update(const Physics &physics) {
    physics.move_object(phobj_);

    if (state_ == Drop::State::DROPPED) {
        fallen_ += 1.0;

        double progress = fallen_ / air_ticks_;

        if (progress >= 1.0) {
            phobj_.hspeed = 0.0;
            state_ = Drop::State::FLOATING;
            angle_.set(0.0f);
            set_position(dest_.x(), dest_.y() - 4);
        } else {
            phobj_.x = launch_.x() + (dest_.x() - launch_.x()) * progress;

            if (fallen_ <= RISE_TICKS) {
                phobj_.y = launch_.y() - rise_offset(fallen_);
            } else {
                phobj_.y = launch_.y() - DROP_RISE
                           + fall_offset(fallen_ - RISE_TICKS);
            }

            static const float SPINSTEP = 0.2f;
            angle_ += SPINSTEP;
        }
    }

    if (state_ == Drop::State::FLOATING) {
        phobj_.y = base_y_ + 5.0f + (cos(moved_) - 1.0f) * 2.5f;
        moved_ = (moved_ < 360.0f) ? moved_ + 0.025f : 0.0f;
    }

    if (state_ == Drop::State::PICKED_UP) {
        // Also how long the item takes to drift in to the player, so slowing the fade slows the
        // flight with it instead of leaving the item half way there when it disappears.
        static const uint16_t PICKUPTIME = 76;
        static const float OPCSTEP = 1.0f / PICKUPTIME;

        if (looter_) {
            double hdelta = looter_->x - phobj_.x;
            phobj_.hspeed =
                looter_->hspeed / 2.0 + (hdelta - 16.0) / PICKUPTIME;
        }

        opacity_ -= OPCSTEP;

        if (opacity_.last() <= OPCSTEP) {
            opacity_.set(1.0f);

            MapObject::deactivate();
            return -1;
        }
    }

    return phobj_.fhlayer;
}

void Drop::expire(int8_t type, const PhysicsObject *lt) {
    switch (type) {
        case 0: state_ = Drop::State::PICKED_UP; break;
        case 1: deactivate(); break;
        case 2:
            angle_.set(0.0f);
            state_ = Drop::State::PICKED_UP;
            looter_ = lt;
            phobj_.vspeed = -4.5f;
            phobj_.type = PhysicsObject::Type::NORMAL;
            break;
    }
}

Rectangle<int16_t> Drop::bounds() const {
    auto lt = get_position();
    auto rb = lt + Point<int16_t>(32, 32);

    return Rectangle<int16_t>(lt, rb);
}

Rectangle<int16_t> Drop::icon_bounds(Point<int16_t> origin,
                                    Point<int16_t> dimensions) const {
    // Matches DrawArgument::get_rectangle with an unstretched, unflipped draw: the sprite starts
    // one origin away from the position and runs for its own width from there.
    Point<int16_t> lt = get_position() - origin;

    return Rectangle<int16_t>(lt, lt + dimensions);
}
}  // namespace ms