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

#include "../../Graphics/Geometry.h"
#include "../../Template/Interpolated.h"

namespace ms {
// A translucent 30x30 time fill clipped to the icon's fixed rounded outline.
class IconCover {
public:
    enum Type { BUFF, COOLDOWN };

    IconCover(Type type, int32_t duration);

    void draw(Point<int16_t> position, float alpha) const;

    // Draw shared cooldown state without starting a separate timer per icon.
    // position is the cover's top-left; remaining_scale is the fraction left.
    static void draw_cooldown(Point<int16_t> position,
                              float remaining_scale,
                              float opacity = 1.0f);

    void update();

private:
    static void draw_fill(const ColorBox &cover,
                          Point<int16_t> position,
                          float scale);

    ColorBox cover_;
    Linear<float> yscale_;
    float scale_step_;
    Type type_;
};
}  // namespace ms