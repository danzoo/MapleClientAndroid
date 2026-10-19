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

#include "FootholdTree.h"

namespace ms {
// Class that uses physics engines and the collection of platforms to determine
// object movement
class Physics {
public:
    Physics(const nl::node &src);

    Physics();

    // Move the specified object over the specified game-time.
    void move_object(PhysicsObject &tomove) const;

    // Determine the point on the ground below the specified position.
    Point<int16_t> get_y_below(Point<int16_t> position) const;

    // The x-range of the stretch of platform the specified foothold is part of: the footholds
    // joined end to end along it, up to the gap or the wall at either end. Empty when the id is
    // not a foothold.
    //
    // Anything shoved sideways along a platform has to be stopped at the ends of this, because the
    // platform lookup follows the chain of footholds off the end of it and lets the object fall.
    struct Platform {
        int16_t left = 0;
        int16_t right = 0;
    };

    Platform get_platform(uint16_t fhid) const;

    // Return a reference to the collection of platforms.
    const FootholdTree &get_fht() const;

private:
    void move_normal(PhysicsObject &) const;

    void move_flying(PhysicsObject &) const;

    void move_swimming(PhysicsObject &) const;

    FootholdTree fh_tree_;
};
}  // namespace ms