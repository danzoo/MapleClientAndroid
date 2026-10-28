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

#include "../Template/Point.h"

namespace ms {
// The mouse cursor. Only its position and state are kept: the interface is driven by touch on
// this platform, so there is no pointer sprite to draw.
class Cursor {
public:
    // Maple-cursorstates, linked to the cursor's animation.
    enum State {
        IDLE,
        CAN_CLICK,
        GAME,
        HOUSE,
        CAN_CLICK2,
        CAN_GRAB,
        // Something the interface will let a drag start on without a held press first. Only the
        // windows that opt in to it report this, so the touch layer still scrolls everywhere else.
        CAN_DRAG,
        GIFT,
        VSCROLL,
        HSCROLL,
        VSCROLL_IDLE,
        HSCROLL_IDLE,
        GRABBING,
        CLICKING,
        RCLICK,
        LEAF = 18,
        CHAT_BAR_VDRAG = 67,
        CHAT_BAR_HDRAG,
        CHAT_BAR_BLTRDRAG,
        CHAT_BAR_MOVE = 72,
        CHAT_BAR_BRTLDRAG,
        LENGTH
    };

    Cursor();

    void set_state(State state);

    void set_position(Point<int16_t> position);

    State get_state() const;

    Point<int16_t> get_position() const;

private:
    State state_;
    Point<int16_t> position_;
};
}  // namespace ms