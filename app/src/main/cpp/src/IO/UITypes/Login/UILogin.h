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

#include "../../Template/BoolPair.h"
#include "../Components/Textfield.h"
#include "../UIElement.h"

namespace ms {
class UILogin : public UIElement {
public:
    static constexpr Type TYPE = UIElement::Type::LOGIN;
    static constexpr bool FOCUSED = false;
    static constexpr bool TOGGLED = false;

    UILogin();

    void draw(float alpha) const override;

    void update() override;

    Cursor::State send_cursor(bool clicked, Point<int16_t> cursor_pos) override;

    bool is_in_range(Point<int16_t> cursorpos) const override;

    UIElement::Type get_type() const override;

protected:
    Button::State button_pressed(uint16_t id) override;

private:
    // Out of sprites_ because that list now goes onto the panel: this is the picture behind the
    // dialog and the screen's own frame, which stay on the interface under it. They are updated by
    // hand since they are no longer in the base list that UIElement::update() walks.
    std::vector<Sprite> sprites_backdrop_;

    void login();

    void open_url(uint16_t id);

    enum Buttons {
        BT_LOGIN,
        BT_REGISTER,
        BT_HOMEPAGE,
        BT_PASSLOST,
        BT_IDLOST,
        BT_SAVEID,
        BT_QUIT,
        NUM_BUTTONS
    };

    Text version_;
    Textfield account_;
    Textfield password_;
    Texture accountbg_;
    Texture passwordbg_;
    BoolPair<Texture> checkbox_;

    bool saveid_;
};
}  // namespace ms