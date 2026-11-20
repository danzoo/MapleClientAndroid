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
#include "IconCover.h"

#include <algorithm>

namespace ms {
IconCover::IconCover(Type t, int32_t duration) {
    cover_ = ColorBox(30, 30, Color::Name::BLACK, 0.6f);

    if (duration <= Constants::TIMESTEP) {
        scale_step_ = 1.0f;
    } else {
        scale_step_ = Constants::TIMESTEP * 1.0f / duration;
    }

    type_ = t;

    switch (type_) {
        case Type::BUFF: yscale_.set(0.0f); break;
        case Type::COOLDOWN: yscale_.set(1.0f); break;
    }
}

void IconCover::draw(Point<int16_t> position, float alpha) const {
    draw_fill(cover_, position, yscale_.get(alpha));
}

void IconCover::draw_cooldown(Point<int16_t> position,
                               float remaining_scale,
                               float opacity) {
    if (!(opacity > 0.0f)) {
        return;
    }

    const ColorBox cover(30, 30, Color::Name::BLACK,
                          0.6f * std::min(opacity, 1.0f));
    draw_fill(cover, position, remaining_scale);
}

void IconCover::draw_fill(const ColorBox &cover,
                          Point<int16_t> position,
                          float scale) {
    // Clamp the draw, not the clock: even short timers must stay in the icon.
    // The comparison also rejects a non-finite NaN scale before integer conversion.
    if (!(scale > 0.0f)) {
        return;
    }
    const auto interheight = static_cast<int16_t>(30 * std::min(scale, 1.0f));
    if (interheight == 0) {
        return;
    }

    // Clip to a fixed icon outline so the moving time edge stays flat.
    // Two corner rows also keep the fill inside icons whose origin.y is 33
    // instead of the usual 32, without moving the shared 30x30 cover area.
    const auto inset_at = [](int16_t row) -> int16_t {
        const int16_t edge = std::min<int16_t>(row, 29 - row);
        return edge < 2 ? 2 - edge : 0;
    };
    int16_t row = 30 - interheight;
    while (row < 30) {
        const int16_t inset = inset_at(row);
        const int16_t start = row;
        do {
            ++row;
        } while (row < 30 && inset_at(row) == inset);

        cover.draw(DrawArgument(position + Point<int16_t>(inset, start),
                                Point<int16_t>(30 - 2 * inset, row - start)));
    }
}

void IconCover::update() {
    switch (type_) {
        case Type::BUFF:
            yscale_ += scale_step_;

            if (yscale_.last() >= 1.0f) {
                yscale_.set(1.0f);
                scale_step_ = 0.0f;
            }

            break;
        case Type::COOLDOWN:
            yscale_ -= scale_step_;

            if (yscale_.last() <= 0.0f) {
                yscale_.set(0.0f);
                scale_step_ = 0.0f;
            }

            break;
    }
}
}  // namespace ms