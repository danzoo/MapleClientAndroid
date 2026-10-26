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

#include <unordered_map>

#include "Foothold.h"
#include "PhysicsObject.h"

namespace ms {
// The collection of platforms in a maple map
// Used for collision-detection
class FootholdTree {
public:
    FootholdTree(const img::node &source);

    FootholdTree();

    // Takes an accelerated PhysicsObject and limits its movement based on the
    // platforms in this tree.
    void limit_movement(PhysicsObject &touse) const;

    // Same check, measured against a displacement given by hand instead of read back off the
    // object's speed. One update taken in pieces needs each piece looked at on its own; asking the
    // whole tick's speed whether it fits would test the end of the tick from the middle of it.
    void limit_movement(PhysicsObject &touse, double hmove, double vmove) const;

    // Updates a PhysicsObject's fhid based on it's position.
    void update_fh(PhysicsObject &touse) const;

    // Determine the point on the ground below the specified position.
    int16_t get_y_below(Point<int16_t> position) const;

    // Determine the platform directly below the specified position, or zero if there is none
    // within the borders of the map.
    uint16_t get_fhid_below(double fx, double fy) const;

    // Returns the platform with the specified identifier, or a null platform.
    const Foothold &get_foothold(uint16_t fhid) const;

    // Returns the leftmost and rightmost platform positions of the map.
    Range<int16_t> get_walls() const;

    // Returns the topmost and bottommost platform positions of the map.
    Range<int16_t> get_borders() const;

private:
    double get_wall(uint16_t fhid, bool left, double fy) const;

    double get_edge(uint16_t fhid, bool left) const;

    const Foothold &get_fh(uint16_t fhid) const;

    std::unordered_map<uint16_t, Foothold> footholds_;
    std::unordered_multimap<int16_t, uint16_t> footholds_by_x_;

    Foothold null_fh_;
    Range<int16_t> walls_;
    Range<int16_t> borders_;
};
}  // namespace ms