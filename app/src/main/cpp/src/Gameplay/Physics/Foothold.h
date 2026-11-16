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

#include "../../Template/Range.h"

namespace ms {
// Represents a platform part on a maple map
class Foothold {
public:
    Foothold(const img::node &src, uint16_t id, uint8_t layer);

    Foothold();

    // Returns the foothold id aka the identifier in game data of this platform
    uint16_t id() const;

    // Returns the platform left to this.
    uint16_t prev() const;

    // Returns the platform right to this.
    uint16_t next() const;

    // Returns the platform's layer.
    uint8_t layer() const;

    // Returns the horizontal component.
    const Range<int16_t> &horizontal() const;

    // Returns the vertical component.
    const Range<int16_t> &vertical() const;

    // Return the left edge
    int16_t l() const;

    // Return the right edge.
    int16_t r() const;

    // Return the top edge.
    int16_t t() const;

    // Return the bottom edge.
    int16_t b() const;

    // Return the first horizontal component.
    int16_t x1() const;

    // Return the second horizontal component.
    int16_t x2() const;

    // Return the first vertical component.
    int16_t y1() const;

    // Return the second vertical component.
    int16_t y2() const;

    // Return if the platform is a wall (x1 == x2).
    bool is_wall() const;

    // Return if the platform is a floor (y1 == y2).
    bool is_floor() const;

    // Return if this platform is a left edge.
    bool is_left_edge() const;

    // Return if this platform is a right edge.
    bool is_right_edge() const;

    // Returns if a x-coordinate is above or below this platform.
    bool hcontains(int16_t x) const;

    // Returns if a y-coordinate is right or left of this platform.
    bool vcontains(int16_t y) const;

    // Check whether this foothold blocks an object.
    bool is_blocking(const Range<int16_t> &vertical) const;

    // Returns the width.
    int16_t hdelta() const;

    // Returns the height.
    int16_t vdelta() const;

    // Returns the slope as a ratio of vertical/horizontal.
    double slope() const;

    // Unit tangent of the surface in the +x direction. The original client
    // keeps footholds normalized this way (cos at node+0x30, sin at +0x38),
    // with cos = 1/sqrt(1+slope^2) and sin = slope*cos.
    double unit_cos() const;
    double unit_sin() const;

    // The WZ `force` of this foothold: a conveyor/slide push in px/s^2, positive to the
    // right and negative to the left. Zero when the property is absent (the usual case).
    //
    // The Data tree has no separate per-foothold friction key, so this is the only surface
    // hint it gives. This client treats footholds carrying `force` as the slippery ones and
    // gives them the archive's minFriction; that mapping is a model choice, while the push
    // itself and the friction bounds are read straight out of the data.
    double force() const;

    bool is_slippery() const;

    // Returns a y-coordinate right below the given x-coordinate.
    double ground_below(double x) const;

private:
    uint16_t m_id_;
    uint16_t m_prev_;
    uint16_t m_next_;
    uint8_t m_layer_;
    double m_force_;
    Range<int16_t> m_horizontal_;
    Range<int16_t> m_vertical_;
};
}  // namespace ms