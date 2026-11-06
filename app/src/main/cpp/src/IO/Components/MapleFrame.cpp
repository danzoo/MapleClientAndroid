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
#include "MapleFrame.h"

#include <algorithm>
#include <imgwz/wz.hpp>

namespace ms {
MapleFrame::MapleFrame(const img::node &src) {
    center_ = src["c"];
    east_ = src["e"];
    north_east_ = src["ne"];
    north_ = src["n"];
    north_west_ = src["nw"];
    west_ = src["w"];
    south_west_ = src["sw"];
    south_ = src["s"];
    south_east_ = src["se"];

    xtile_ = std::max<int16_t>(north_.width(), 1);
    ytile_ = std::max<int16_t>(west_.height(), 1);
}

MapleFrame::MapleFrame() = default;

MapleFrame::Box MapleFrame::inner_box(Point<int16_t> position,
                                      int16_t rwidth,
                                      int16_t rheight) const {
    int16_t numhor = rwidth / xtile_ + 2;
    int16_t numver = rheight / ytile_;
    int16_t width = numhor * xtile_;
    int16_t height = numver * ytile_;
    int16_t left = position.x() - width / 2;
    int16_t top = position.y() - height;

    return Box {
        static_cast<int16_t>(left),
        static_cast<int16_t>(top),
        static_cast<int16_t>(left + width),
        static_cast<int16_t>(top + height)
    };
}

void MapleFrame::draw(Point<int16_t> position,
                      int16_t rwidth,
                      int16_t rheight) const {
    Box box = inner_box(position, rwidth, rheight);
    int16_t left = box.left;
    int16_t top = box.top;
    int16_t right = box.right;
    int16_t bottom = box.bottom;
    int16_t width = right - left;
    int16_t height = bottom - top;

    north_west_.draw(DrawArgument(left, top));
    south_west_.draw(DrawArgument(left, bottom));

    // A one-pixel-thick edge is art meant to be stretched, not repeated: tiling it emits one
    // degenerate quad per pixel and, because the interface is magnified by a fractional UI_SCALE
    // on its way to the native-resolution target, those quads stop abutting and the border reads as
    // a comb. Wider tiles really are repeated, so they keep the loop.
    if (ytile_ == 1) {
        west_.draw(
            DrawArgument(Point<int16_t>(left, top),
                         Point<int16_t>(west_.width(), height)));
        east_.draw(
            DrawArgument(Point<int16_t>(right, top),
                         Point<int16_t>(east_.width(), height)));
    } else {
        for (int y = top; y < bottom; y += ytile_) {
            west_.draw(DrawArgument(left, y));
            east_.draw(DrawArgument(right, y));
        }
    }

    center_.draw(
        DrawArgument(Point<int16_t>(left, top), Point<int16_t>(width, height)));

    if (xtile_ == 1) {
        north_.draw(
            DrawArgument(Point<int16_t>(left, top),
                         Point<int16_t>(width, north_.height())));
        south_.draw(
            DrawArgument(Point<int16_t>(left, bottom),
                         Point<int16_t>(width, south_.height())));
    } else {
        for (int x = left; x < right; x += xtile_) {
            north_.draw(DrawArgument(x, top));
            south_.draw(DrawArgument(x, bottom));
        }
    }

    north_east_.draw(DrawArgument(right, top));
    south_east_.draw(DrawArgument(right, bottom));
}

void MapleFrame::build(NativeTooltip::List &list,
                       Point<int16_t> position,
                       int16_t rwidth,
                       int16_t rheight,
                       const std::string &asset) const {
    Box box = inner_box(position, rwidth, rheight);
    int16_t left = box.left;
    int16_t top = box.top;
    int16_t right = box.right;
    int16_t bottom = box.bottom;
    int16_t width = right - left;
    int16_t height = bottom - top;

    list.picture(north_west_, DrawArgument(left, top), asset + "/nw");
    list.picture(south_west_, DrawArgument(left, bottom), asset + "/sw");

    if (ytile_ == 1) {
        list.picture(west_,
                     DrawArgument(Point<int16_t>(left, top),
                                  Point<int16_t>(west_.width(), height)),
                     asset + "/w");
        list.picture(east_,
                     DrawArgument(Point<int16_t>(right, top),
                                  Point<int16_t>(east_.width(), height)),
                     asset + "/e");
    }
    else {
        for (int y = top; y < bottom; y += ytile_) {
            list.picture(west_, DrawArgument(left, y), asset + "/w");
            list.picture(east_, DrawArgument(right, y), asset + "/e");
        }
    }

    list.picture(center_,
                 DrawArgument(Point<int16_t>(left, top),
                              Point<int16_t>(width, height)),
                 asset + "/c");

    if (xtile_ == 1) {
        list.picture(north_,
                     DrawArgument(Point<int16_t>(left, top),
                                  Point<int16_t>(width, north_.height())),
                     asset + "/n");
        list.picture(south_,
                     DrawArgument(Point<int16_t>(left, bottom),
                                  Point<int16_t>(width, south_.height())),
                     asset + "/s");
    }
    else {
        for (int x = left; x < right; x += xtile_) {
            list.picture(north_, DrawArgument(x, top), asset + "/n");
            list.picture(south_, DrawArgument(x, bottom), asset + "/s");
        }
    }

    list.picture(north_east_, DrawArgument(right, top), asset + "/ne");
    list.picture(south_east_, DrawArgument(right, bottom), asset + "/se");
}
}  // namespace ms