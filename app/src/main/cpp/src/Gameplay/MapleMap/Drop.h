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

#include "../Template/Interpolated.h"
#include "../Template/Rectangle.h"
#include "MapObject.h"

namespace ms {
class Drop : public MapObject {
public:
    int8_t update(const Physics &physics) override;

    void expire(int8_t, const PhysicsObject *);

    virtual Rectangle<int16_t> bounds() const;

    // Whether the item has already been claimed and is flying to whoever claimed it. It is still
    // drawn and still active while that happens, so without this a pile keeps handing out the same
    // item until the flight has faded.
    bool claimed() const;

protected:
    Drop(int32_t oid,
         int32_t owner,
         Point<int16_t> start,
         Point<int16_t> dest,
         int8_t type,
         int8_t mode,
         bool playerdrop);

    // The box an icon covers on screen, laid out exactly the way the renderer lays it out. The
    // position a drop is stored at is only one point of that box, so measuring from it makes an
    // item look nearer on one side of the character than on the other.
    Rectangle<int16_t> icon_bounds(Point<int16_t> origin,
                                  Point<int16_t> dimensions) const;

    // What to draw an icon from so that it sits centred over the drop's own position. Icons hang off
    // their origin, which puts the sprite to one side of the point it is stored at: an item coming
    // out of the character appeared to leave from beside them rather than from them. Only the width
    // is squared up, because the height an icon hangs at is what makes it rest on the ground.
    static Point<int16_t> icon_shift(Point<int16_t> origin,
                                     Point<int16_t> dimensions);

    Linear<float> opacity_;
    Linear<float> angle_;

private:
    enum State { DROPPED, FLOATING, PICKED_UP };

    int32_t owner_;
    int8_t pickup_type_;
    bool player_drop_;

    const PhysicsObject *looter_;
    State state_;

    Point<int16_t> dest_;
    Point<int16_t> launch_;
    // The height the current flight peaks at, how many seconds it takes to get there, and how high
    // above the ground it was thrown. The height picks the gravity the rest of the flight is
    // computed with, so the toss out of a mob and the hop to whoever picks it up are separate
    // routes.
    double apex_;
    double rise_seconds_;
    double rise_;
    double base_y_;
    double moved_;
    // Seconds of the current flight that have gone by, and how long the whole flight takes.
    double fallen_;
    double air_seconds_;
};
}  // namespace ms