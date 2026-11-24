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
#include "Bullet.h"

#include <cmath>
#include <utility>

#include "../../Constants.h"
#include "../Physics/FootholdTree.h"

namespace ms {

Point<int16_t> Bullet::muzzle(Point<int16_t> origin, bool toleft) {
    return Point<int16_t>(origin.x() + (toleft ? -Constants::MUZZLE_X : Constants::MUZZLE_X),
                          origin.y() + Constants::SHOOT_LINE_Y);
}

Bullet::Bullet(Animation a,
               Point<int16_t> origin,
               bool toleft,
               const FootholdTree &terrain) :
    animation_(std::move(a)),
    terrain_(&terrain),
    flip_(toleft) {
    const Point<int16_t> start = muzzle(origin, toleft);

    move_obj_.set_x(start.x());
    move_obj_.set_y(start.y());
}

void Bullet::draw(double viewx, double viewy, float alpha) const {
    Point<int16_t> bulletpos = move_obj_.get_absolute(viewx, viewy, alpha);
    DrawArgument args(bulletpos, flip_);
    animation_.draw(args, alpha);
}

bool Bullet::settarget(Point<int16_t> target) {
    double xdelta = target.x() - move_obj_.crnt_x();
    double ydelta = target.y() - move_obj_.crnt_y();

    if (std::abs(xdelta) < 10.0) {
        return true;
    }

    flip_ = xdelta > 0.0;

    // Normalize the firing direction so every shot travels at the same speed,
    // regardless of target distance or height. Only the flight duration changes.
    const double distance = std::hypot(xdelta, ydelta);
    move_obj_.hspeed = Constants::BULLET_SPEED * xdelta / distance;
    move_obj_.vspeed = Constants::BULLET_SPEED * ydelta / distance;

    return false;
}

Bullet::Flight Bullet::update(Point<int16_t> target) {
    animation_.update();

    const Point<int16_t> before = move_obj_.get_position();

    move_obj_.move();

    const Point<int16_t> after = move_obj_.get_position();

    // A wall this step runs into ends the flight where it meets it, not where the step was
    // heading.
    Point<int16_t> wall;

    if (terrain_->first_crossing(before, after, wall)) {
        move_obj_.set_x(wall.x());
        move_obj_.set_y(wall.y());

        return Flight::BLOCKED;
    }

    int16_t xdelta = target.x() - move_obj_.get_x();
    bool arrived = move_obj_.hspeed > 0.0 ? xdelta < 10 : xdelta > 10;

    return arrived ? Flight::ARRIVED : Flight::FLYING;
}
}  // namespace ms