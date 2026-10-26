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

// Game attributes - tunable values that describe how the game itself behaves, as opposed to how a
// particular input device is driven. Anything that a designer would adjust for feel belongs here,
// no matter which module happens to read it.
namespace ms::GameAttributes {
    // Loot has to be right at the feet. Counted to the nearest edge of the item's picture rather
    // than to the point it is stored at, which is what made the reach differ by side.
    constexpr int16_t PICKUP_RANGE = 24;
    constexpr int16_t TALK_RANGE = 350;

    // How wide a band around a rope or ladder still counts as standing at it. The reference client's
    // ten pixels are a mouse-width target; on a phone the position is aimed with a joystick, so the
    // grab has to forgive noticeably more. Only the grab reads this - once it lands the character is
    // snapped onto the rope's own x, so a wide window cannot make it drift.
    constexpr int16_t CLIMB_RANGE_X = 16;

    // The vertical margin a rope's two ends are measured with. One number serves both the grab and
    // the release test, and it has to stay one: a grab allowed inside a wider band than the release
    // is tested with is cancelled on the next tick, and with the climb cooldown that reads as being
    // dropped off the rope.
    constexpr int16_t CLIMB_MARGIN_Y = 8;

    // The size the login and character selection screens are designed around.
    constexpr int16_t LOGIN_WIDTH = 800;
    constexpr int16_t LOGIN_HEIGHT = 600;
}  // namespace ms::GameAttributes
