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

#include <map>
#include <vector>

#include "../Template/EnumMap.h"
#include "Layer.h"
#include "Obj.h"
#include "Tile.h"

namespace ms {
// A tile and obj layer.
class TilesObjs {
public:
    TilesObjs(const img::node &src);

    TilesObjs();

    void draw(Point<int16_t> viewpos, float alpha) const;

    void update();

    // Damage of the first trap whose hitbox overlaps the body, or 0 when none does.
    int16_t trap_damage_at(const Rectangle<int16_t> &body) const;

private:
    std::multimap<uint8_t, Tile> tiles_;
    std::multimap<uint8_t, Obj> objs_;
};

// The collection of tile and obj layers on a map.
class MapTilesObjs {
public:
    MapTilesObjs(const img::node &src);

    MapTilesObjs();

    void draw(Layer::Id layer, Point<int16_t> viewpos, float alpha) const;

    void update();

    // Damage of the first trap across all layers whose hitbox overlaps the body, or 0.
    int16_t trap_damage_at(const Rectangle<int16_t> &body) const;

private:
    EnumMap<Layer::Id, TilesObjs> layers_;
};
}  // namespace ms