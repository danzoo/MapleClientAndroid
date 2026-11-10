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
#include "Obj.h"

#include <imgwz/wz.hpp>

namespace ms {
Obj::Obj(const img::node &src) {
    auto node = img::wz::map["Obj"][src["oS"] + ".img"][src["l0"]][src["l1"]]
                              [src["l2"]];
    animation_ = Animation(node);
    pos_ = Point<int16_t>(src["x"], src["y"]);
    flip_ = src["f"].get_bool();
    z_ = src["z"];

    // Only the static traps (thorns/spikes, ice fixtures) carry a frame-0 lt/rb hitbox; the animated
    // ice/laser sets define none, so leave those inert rather than build a zero-area box at the anchor.
    if (src["oS"].get_string() == "trap" && node["0"]["lt"] && node["0"]["rb"]) {
        trap_damage_ = node["damage"];
        Point<int16_t> lt = node["0"]["lt"];
        Point<int16_t> rb = node["0"]["rb"];
        int16_t left = flip_ ? pos_.x() - rb.x() : pos_.x() + lt.x();
        int16_t right = flip_ ? pos_.x() - lt.x() : pos_.x() + rb.x();
        trap_box_ = Rectangle<int16_t>(left, right, pos_.y() + lt.y(), pos_.y() + rb.y());
    }
}

int16_t Obj::trap_damage_at(const Rectangle<int16_t> &body) const {
    if (trap_damage_ > 0 && trap_box_.overlaps(body)) {
        return trap_damage_;
    }
    return 0;
}

void Obj::update() {
    animation_.update();
}

void Obj::draw(Point<int16_t> viewpos, float inter) const {
    animation_.draw(DrawArgument(pos_ + viewpos, flip_), inter);
}

uint8_t Obj::getz() const {
    return z_;
}
}  // namespace ms