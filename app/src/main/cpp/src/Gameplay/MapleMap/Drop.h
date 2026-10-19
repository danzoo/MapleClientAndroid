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
    // The height the flight peaks at and how long it takes to get there from the corpse.
    double apex_;
    double rise_ticks_;
    double base_y_;
    double moved_;
    double fallen_;
    double air_ticks_;
};
}  // namespace ms