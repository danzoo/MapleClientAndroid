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

#include <algorithm>
#include <cmath>
#include <utility>

#include "../../Constants.h"

namespace ms {

Bullet::Bullet(Animation a, Point<int16_t> origin, bool toleft) :
    animation_(std::move(a)),
    flip_(toleft) {
    move_obj_.set_x(origin.x() + (toleft ? -30.0 : 30.0));
    move_obj_.set_y(origin.y() - 26.0);
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

    // The whole distance is covered over the flight time, kept between the two speeds a projectile is
    // allowed to go.
    const double sign = xdelta > 0.0 ? 1.0 : -1.0;
    const double speed = std::clamp(
        std::abs(xdelta) * 1000.0 / Constants::BULLET_FLIGHT_MS,
        Constants::BULLET_MIN_SPEED,
        Constants::BULLET_MAX_SPEED);

    move_obj_.hspeed = sign * speed;
    // The fall is kept at the same angle as the flight, which is a ratio and so needs no units.
    move_obj_.vspeed = move_obj_.hspeed * ydelta / xdelta;

    return false;
}

bool Bullet::update(Point<int16_t> target) {
    animation_.update();
    move_obj_.move();

    int16_t xdelta = target.x() - move_obj_.get_x();
    return move_obj_.hspeed > 0.0 ? xdelta < 10 : xdelta > 10;
}
}  // namespace ms