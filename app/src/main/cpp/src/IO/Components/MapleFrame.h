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

#include <imgwz/node.hpp>

#include "../Graphics/Texture.h"
#include "../NativeTooltip.h"

namespace ms {
class MapleFrame {
public:
    // The rectangle a frame leaves for its content. The center texture is stretched over exactly
    // this, and every border piece is drawn outside of it, so text that stays inside is guaranteed
    // to stay inside the art.
    struct Box {
        int16_t left;
        int16_t top;
        int16_t right;
        int16_t bottom;
    };

    MapleFrame(const img::node &src);

    MapleFrame();

    void draw(Point<int16_t> position, int16_t width, int16_t height) const;

    // The same frame written out for the native window. The nine pieces keep the order they were
    // drawn in, because some of them are drawn over others - a frame that arranged them neatly
    // would put different art on top and read as a different frame.
    void build(NativeTooltip::List &list,
               Point<int16_t> position,
               int16_t width,
               int16_t height,
               const std::string &asset) const;

    Box inner_box(Point<int16_t> position, int16_t width, int16_t height) const;

private:
    Texture center_;
    Texture east_;
    Texture north_east_;
    Texture north_;
    Texture north_west_;
    Texture west_;
    Texture south_west_;
    Texture south_;
    Texture south_east_;
    int16_t xtile_;
    int16_t ytile_;
};
}  // namespace ms