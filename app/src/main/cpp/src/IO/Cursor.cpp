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
#include "Cursor.h"

namespace ms {
Cursor::Cursor() {
    state_ = Cursor::State::IDLE;
}

void Cursor::set_state(State s) {
    state_ = s;
}

void Cursor::set_position(Point<int16_t> pos) {
    position_ = pos;
}

Cursor::State Cursor::get_state() const {
    return state_;
}

Point<int16_t> Cursor::get_position() const {
    return position_;
}
}  // namespace ms