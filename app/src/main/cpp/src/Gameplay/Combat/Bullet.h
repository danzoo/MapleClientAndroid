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

#include <cstdint>

#include "../Graphics/Animation.h"
#include "../Physics/PhysicsObject.h"
#include "../Template/Point.h"

namespace ms {
// Represents a projectile on a map.
class Bullet {
public:
    enum class Flight { FLYING, ARRIVED };

    // Where a shot leaves the character.
    static Point<int16_t> muzzle(Point<int16_t> origin, bool toleft);

    Bullet(Animation animation,
           Point<int16_t> origin,
           bool toleft);

    void draw(double viewx, double viewy, float alpha) const;

    bool settarget(Point<int16_t> target);

    Flight update(Point<int16_t> target);

private:
    Animation animation_;
    MovingObject move_obj_;
    bool flip_;
    float angle_ = 0.0f;
};
}  // namespace ms