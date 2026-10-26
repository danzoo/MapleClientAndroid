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
#include "MapEffect.h"

#include <imgwz/wz.hpp>
#include <utility>

#include "../../Constants.h"

namespace ms {
MapEffect::MapEffect(std::string path) : active_(false) {
    img::node Effect = img::wz::map["Effect.img"];

    effect_ = Effect.resolve(std::move(path));

    int16_t width = Constants::Constants::get().get_viewwidth();

    position_ = Point<int16_t>(width / 2, 250);
}

MapEffect::MapEffect() = default;

void MapEffect::draw() const {
    if (!active_) {
        effect_.draw(position_, 1.0f);
    }
}

void MapEffect::update() {
    if (!active_) {
        // Three quarters of real time - the six milliseconds of its own timeline an effect used to
        // walk for every eight millisecond tick. The api takes whole milliseconds, so it is ten.
        active_ = effect_.update(Constants::REDUCED_ANIMATION_MS);
    }
}
}  // namespace ms