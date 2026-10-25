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

// Pixels per tick the fall stops accelerating at. Without it an item dropped from a high platform
// onto a low one covered the gap in a handful of ticks, which read as a snap rather than a fall.
constexpr double DROP_MAX_FALL = 3.0;

// The rise is half of the level-ground flight, and the same acceleration governs both halves of
// every flight so the arc is one curve.
constexpr double RISE_TICKS = DROP_TICKS / 2.0;

// The acceleration that carries a peak of this height through the rise in RISE_TICKS. Picking it
// from the height is what keeps a flight over level ground at DROP_TICKS however high the peak is
// set, so raising the arc does not slow the drop down with it.
constexpr double arc_accel(double rise) {
    return 2.0 * rise / (RISE_TICKS * RISE_TICKS);
}

// One ballistic route: how high it peaks, and the gravity that carries it there and back down.
struct Arc {
    double rise;
    double accel;
    // Where the fall reaches DROP_MAX_FALL, and how far it has fallen by then.
    double cap_ticks;
    double cap_dist;

    constexpr Arc(double r) :
        rise(r),
        accel(arc_accel(r)),
        cap_ticks(DROP_MAX_FALL / arc_accel(r)),
        cap_dist(DROP_MAX_FALL * DROP_MAX_FALL / (2.0 * arc_accel(r))) {}

    // Ticks spent falling from the peak through the height of the destination below it.
    double ticks(double distance) const {
        if (distance <= cap_dist) {
            return std::sqrt(2.0 * distance / accel);
        }

        return cap_ticks + (distance - cap_dist) / DROP_MAX_FALL;
    }

    // Height below the peak after ticks of the fall.
    double offset(double ticks) const {
        if (ticks <= cap_ticks) {
            return 0.5 * accel * ticks * ticks;
        }

        return cap_dist + DROP_MAX_FALL * (ticks - cap_ticks);
    }
};

// How far above the ground an item is carried when it comes out of a mob or out of the character's
// bag. Kept away from the destination so an item that ends up on a platform below still gets the
// same toss up out of the mob, rather than having its arc flattened into a fall by the height it
// has left to drop.
constexpr Arc SPAWN(56.0);

// The hop an item gets when someone picks it up. Lower than the toss it was thrown with, because
// being collected reads as handed over rather than as thrown again.
constexpr Arc PICKUP(42.0);

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
    rise_ = PICKUP.rise;

    switch (mode) {
        case 0:
        case 1: {
            state_ = Drop::State::DROPPED;
            base_y_ = static_cast<double>(dest_.y() - 4);
            // The walk to the destination is animated directly rather than handed to the terrain,
            // because a launch speed picked from the drop's height above the ground overshot the
            // spot the server measured the pickup from and the item got snapped back the moment it
            // touched down.
            launch_ = Point<int16_t>(start.x(), start.y() - 4);
            fallen_ = 0.0;

            // The peak sits one spawn rise above whichever end of the flight is higher, so an item
            // the server put on a step above the corpse is thrown onto it instead of running out of
            // flight at the corpse's own height and jumping the last step. Over level ground and
            // downhill this is the launch point minus the rise.
            const double landing = dest_.y() - 4;

            rise_ = SPAWN.rise;
            apex_ = std::min(static_cast<double>(launch_.y()), landing) - SPAWN.rise;
            rise_ticks_ = std::sqrt(2.0 * (launch_.y() - apex_) / SPAWN.accel);
            air_ticks_ = rise_ticks_ + SPAWN.ticks(landing - apex_);

            phobj_.type = PhysicsObject::Type::FIXATED;
            break;
        }
        case 2:
            state_ = Drop::State::FLOATING;
            base_y_ = phobj_.crnt_y();
            phobj_.type = PhysicsObject::Type::FIXATED;
            break;
        case 3:
            state_ = Drop::State::PICKED_UP;
            // The drop is fixated by now, so this is not physics: it is the height the item is
            // lifted as it leaves for the hand.
            phobj_.set_vstep(-5.0f);
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

            // Layered with what it landed on rather than with where it was thrown from. The
            // foothold is pinned for the whole flight, so an item that came off something up on a
            // bridge and landed on the ground would keep the bridge's layer and be drawn over the
            // scenery the ground is behind.
            const uint16_t landed =
                physics.get_fht().get_fhid_below(dest_.x(), dest_.y() - 4);

            if (landed) {
                phobj_.fhid = landed;
                phobj_.fhlayer = physics.get_fht().get_foothold(landed).layer();
            }
        } else {
            phobj_.x = launch_.x() + (dest_.x() - launch_.x()) * progress;

            const Arc arc(rise_);

            if (fallen_ <= rise_ticks_) {
                // The rise is the fall run backwards from the peak, so both halves are one curve.
                const double since = fallen_ - rise_ticks_;

                phobj_.y = apex_ + 0.5 * arc.accel * since * since;
            } else {
                phobj_.y = apex_ + arc.offset(fallen_ - rise_ticks_);
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
        // The fade is spread over exactly the flight, so the item reaches the hand at the moment it
        // disappears. With nothing to fly to (someone else's loot, or a plain despawn) it fades out
        // where it lies, over the length of a toss across level ground.
        const float step = looter_ ? static_cast<float>(1.0 / air_ticks_)
                                   : static_cast<float>(1.0 / DROP_TICKS);

        if (looter_) {
            fallen_ += 1.0;

            const double progress = fallen_ / air_ticks_;

            // Aimed at where the hand is now rather than where it was when the item left the ground,
            // so walking off with a flight already under way still ends at the character.
            const double handx = looter_->x.get();

            phobj_.x = launch_.x() + (handx - launch_.x()) * progress;

            const Arc arc(rise_);

            if (fallen_ <= rise_ticks_) {
                const double since = fallen_ - rise_ticks_;

                phobj_.y = apex_ + 0.5 * arc.accel * since * since;
            } else {
                phobj_.y = apex_ + arc.offset(fallen_ - rise_ticks_);
            }
        }

        opacity_ -= step;

        if (opacity_.last() <= step) {
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

            // The toss it got coming out of the mob, aimed at the hand instead of at the ground and
            // peaked a little lower, because being collected reads as handed over rather than as
            // thrown again. Gravity is kept out of it, otherwise it decides how long the hop takes
            // and the flight ends well before the fade does.
            launch_ = phobj_.get_position();
            fallen_ = 0.0;

            {
                const double landing =
                    lt ? lt->y.get() : static_cast<double>(launch_.y());

                rise_ = PICKUP.rise;
                apex_ = std::min(static_cast<double>(launch_.y()), landing) - PICKUP.rise;
                rise_ticks_ = std::sqrt(2.0 * (launch_.y() - apex_) / PICKUP.accel);
                air_ticks_ = rise_ticks_ + PICKUP.ticks(landing - apex_);
            }

            phobj_.hspeed = 0.0;
            phobj_.vspeed = 0.0;
            phobj_.type = PhysicsObject::Type::FIXATED;
            break;
    }
}

Rectangle<int16_t> Drop::bounds() const {
    auto lt = get_position();
    auto rb = lt + Point<int16_t>(32, 32);

    return Rectangle<int16_t>(lt, rb);
}

bool Drop::claimed() const {
    return state_ == Drop::State::PICKED_UP;
}

Point<int16_t> Drop::icon_shift(Point<int16_t> origin,
                                Point<int16_t> dimensions) {
    return Point<int16_t>(dimensions.x() / 2 - origin.x(), 0);
}

Rectangle<int16_t> Drop::icon_bounds(Point<int16_t> origin,
                                    Point<int16_t> dimensions) const {
    // Matches DrawArgument::get_rectangle with an unstretched, unflipped draw: the sprite starts
    // one origin away from the position it is drawn from and runs for its own width from there.
    Point<int16_t> lt = get_position() - origin - icon_shift(origin, dimensions);

    return Rectangle<int16_t>(lt, lt + dimensions);
}
}  // namespace ms