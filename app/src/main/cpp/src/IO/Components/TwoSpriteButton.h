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

#include "../../Graphics/Texture.h"
#include "../NativeNpcTalk.h"
#include "../Template/BoolPair.h"
#include "Button.h"

namespace ms {
class TwoSpriteButton : public Button {
public:
    TwoSpriteButton(const img::node &nsrc,
                    const img::node &ssrc,
                    Point<int16_t> npos,
                    Point<int16_t> spos);

    TwoSpriteButton(const img::node &nsrc,
                    const img::node &ssrc,
                    Point<int16_t> position);

    TwoSpriteButton(const img::node &nsrc, const img::node &ssrc);

    TwoSpriteButton();

    void draw(Point<int16_t> position) const override;

    // The same piece of art as a record of a native window's display list. Only this button's two
    // frames are said, and which is being worn: the window is handed names, so the caller has to
    // name both the plain and the highlighted one.
    void build(NativeNpcTalk::List &list,
               Point<int16_t> parentpos,
               const std::string &normal_asset,
               const std::string &selected_asset) const;

    void update() override {}

    Rectangle<int16_t> bounds(Point<int16_t> position) const override;

    int16_t width() const override;

    Point<int16_t> origin() const override;

    Cursor::State send_cursor(bool, Point<int16_t>) override {
        return Cursor::State::IDLE;
    }

private:
    BoolPair<Texture> textures_;
    Point<int16_t> npos_;
    Point<int16_t> spos_;
};
}  // namespace ms