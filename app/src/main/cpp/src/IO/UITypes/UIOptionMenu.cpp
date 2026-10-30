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
#include "UIOptionMenu.h"

#include <imgwz/wz.hpp>

#include "../Components/MapleButton.h"
#include "../Components/TwoSpriteButton.h"
#include "../KeyAction.h"

namespace ms {
UIOptionMenu::UIOptionMenu() :
    UIDragElement<PosOPTIONMENU>(),
    selected_tab_(0) {
    img::node OptionMenu = img::wz::ui["StatusBar3.img"]["OptionMenu"];
    img::node backgrnd = OptionMenu["backgrnd"];

    sprites_.emplace_back(backgrnd);
    sprites_.emplace_back(OptionMenu["backgrnd2"]);

    tab_background_[Buttons::TAB0] = OptionMenu["sound"]["layer:backgrnd"];
    tab_background_[Buttons::TAB1] = OptionMenu["game"]["layer:backgrnd"];
    tab_background_[Buttons::TAB2] = OptionMenu["invite"]["layer:backgrnd"];

    buttons_[Buttons::CANCEL] =
        std::make_unique<MapleButton>(OptionMenu["button:Cancel"]);
    buttons_[Buttons::OK] =
        std::make_unique<MapleButton>(OptionMenu["button:OK"]);
    buttons_[Buttons::UIRESET] =
        std::make_unique<MapleButton>(OptionMenu["button:UIReset"]);

    img::node tab = OptionMenu["tab"];
    img::node tab_disabled = tab["disabled"];
    img::node tab_enabled = tab["enabled"];

    for (size_t i = Buttons::TAB0; i < Buttons::CANCEL; i++) {
        buttons_[i] =
            std::make_unique<TwoSpriteButton>(tab_disabled[i], tab_enabled[i]);
    }

    Point<int16_t> bg_dimensions = Texture(backgrnd).get_dimensions();

    dimension_ = bg_dimensions;
    drag_area_ = Point<int16_t>(bg_dimensions.x(), 20);

    change_tab(Buttons::TAB1);
}

void UIOptionMenu::draw(float inter) const {
    UIElement::draw_sprites(inter);

    tab_background_[selected_tab_].draw(position_);

    UIElement::draw_buttons(inter);
}

Button::State UIOptionMenu::button_pressed(uint16_t buttonid) {
    switch (buttonid) {
        case Buttons::TAB0:
        case Buttons::TAB1:
        case Buttons::TAB2:
            change_tab(buttonid);
            return Button::State::IDENTITY;
        case Buttons::CANCEL:
        case Buttons::OK: deactivate(); return Button::State::NORMAL;
        case Buttons::UIRESET: return Button::State::DISABLED;
        default: return Button::State::DISABLED;
    }
}

void UIOptionMenu::send_key(int32_t keycode, bool pressed, bool escape) {
    if (pressed) {
        if (escape) {
            deactivate();
        } else if (keycode == KeyAction::Id::RETURN) {
            button_pressed(Buttons::OK);
        }
    }
}

UIElement::Type UIOptionMenu::get_type() const {
    return TYPE;
}

void UIOptionMenu::change_tab(uint16_t tabid) {
    buttons_[selected_tab_]->set_state(Button::State::NORMAL);
    buttons_[tabid]->set_state(Button::State::PRESSED);

    selected_tab_ = tabid;
}
}  // namespace ms
