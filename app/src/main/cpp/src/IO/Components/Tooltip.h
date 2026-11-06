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

#include "../Template/Point.h"
#include "../NativeTooltip.h"

namespace ms {
// Interface for tooltips, information windows about something
// the mouse cursor is pointed at.
class Tooltip {
public:
    // How far text stands off the edge it is kept inside of: enough that a line which does use its
    // entire wrap still finishes short of the border art.
    static constexpr int16_t TEXT_MARGIN = 8;

    // Possible parent UIs for Tooltips.
    enum Parent {
        NONE,
        EQUIP_INVENTORY,
        ITEM_INVENTORY,
        SKILL_BOOK,
        SHOP,
        EVENT,
        TEXT,
        KEY_CONFIG,
        GAMEPAD_CONFIG,
        WORLD_MAP,
        MINI_MAP
    };

    virtual ~Tooltip() = default;

    // Painting the preview into the game's own pass. A preview that the native window has taken over
    // has nothing to draw here and leaves it alone.
    virtual void draw(Point<int16_t> cursorpos) const;

    // The same preview written out for the native window: the pieces it is made of, in the order
    // they would have been drawn, in the units the interface lays itself out in. A preview that has
    // not been converted says so by returning false, and is drawn by the pass above as it always
    // was.
    virtual bool build(NativeTooltip::List &list, Point<int16_t> cursorpos) const;
};
}  // namespace ms