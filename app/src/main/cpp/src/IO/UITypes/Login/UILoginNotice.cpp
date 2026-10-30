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
#include "UILoginNotice.h"

#include <imgwz/wz.hpp>
#include <utility>

#include "../../../Constants.h"
#include "../../../Graphics/PanelRenderer.h"
#include "../Audio/Audio.h"
#include "../Components/MapleButton.h"
#include "../UI.h"
#include "../Window.h"
#include "KeyAction.h"

namespace ms {
UILoginNotice::UILoginNotice(uint16_t message,
                             std::function<void()> okhandler,
                             std::function<void()> cancelhandler) :
    saveid_(false),
    multiple_(false),
    okhandler_(std::move(okhandler)),
    cancel_handler_(std::move(cancelhandler)) {
    img::node Notice = img::wz::ui["Login.img"]["Notice"];
    img::node backgrnd;

    switch (message) {
        case Message::NAME_IN_USE:
        case Message::ILLEGAL_NAME:
        case Message::BLOCKED_ID:
        case Message::INCORRECT_PIC: backgrnd = Notice["backgrnd"]["1"]; break;
        default: backgrnd = Notice["backgrnd"]["0"]; break;
    }

    sprites_.emplace_back(backgrnd);
    sprites_.emplace_back(Notice["text"][message], Point<int16_t>(17, 13));

    if (message == Message::DELETE_CONFIRMATION) {
        multiple_ = true;

        buttons_[Buttons::YES] =
            std::make_unique<MapleButton>(Notice["BtYes"],
                                          Point<int16_t>(70, 106));
        buttons_[Buttons::NO] =
            std::make_unique<MapleButton>(Notice["BtNo"],
                                          Point<int16_t>(130, 106));
    } else {
        buttons_[Buttons::YES] =
            std::make_unique<MapleButton>(Notice["BtYes"],
                                          Point<int16_t>(100, 106));
    }

    position_ = Point<int16_t>(275, 209);
    dimension_ = Texture(backgrnd).get_dimensions();

    // The dialog this notice stands over is on the panel, and that pass is composited over the
    // interface, so the notice goes onto it too - grown about its own centre, the same way the
    // dialog is. Being drawn after the dialog, it lands on top of it inside that pass.
    Point<int16_t> centre =
        position_ + Point<int16_t>(dimension_.x() / 2, dimension_.y() / 2);

    position_ = position_ + Constants::to_panel_space(centre) - centre;
}

void UILoginNotice::draw(float inter) const {
    PanelRenderer::get().begin();

    UIElement::draw(inter);

    PanelRenderer::get().end();
}

Cursor::State UILoginNotice::send_cursor(bool clicked,
                                        Point<int16_t> cursorpos) {
    return UIElement::send_cursor(clicked,
                                  Constants::to_panel_space(cursorpos));
}

bool UILoginNotice::is_in_range(Point<int16_t> cursorpos) const {
    return UIElement::is_in_range(Constants::to_panel_space(cursorpos));
}

UILoginNotice::UILoginNotice(uint16_t message,
                             std::function<void()> okhandler) :
    UILoginNotice(message, std::move(okhandler), []() {}) {}

UILoginNotice::UILoginNotice(uint16_t message) :
    UILoginNotice(message, []() {}) {}

void UILoginNotice::send_key(int32_t keycode, bool pressed, bool escape) {
    if (pressed) {
        if (escape) {
            if (!multiple_) {
                okhandler_();
            } else {
                cancel_handler_();
            }

            deactivate();
        } else if (keycode == KeyAction::RETURN) {
            okhandler_();
            deactivate();
        }
    }
}

UIElement::Type UILoginNotice::get_type() const {
    return TYPE;
}

Button::State UILoginNotice::button_pressed(uint16_t buttonid) {
    if (buttonid == Buttons::YES) {
        okhandler_();
    } else if (buttonid == Buttons::NO) {
        cancel_handler_();
    }

    deactivate();

    return Button::State::NORMAL;
}

UIQuitConfirm::UIQuitConfirm() {
    img::node notice = img::wz::ui["Login.img"]["Notice"];
    img::node backgrnd = notice["backgrnd"]["0"];

    sprites_.emplace_back(backgrnd);
    sprites_.emplace_back(notice["text"][UILoginNotice::Message::CONFIRM_EXIT],
                          Point<int16_t>(17, 13));

    buttons_[Buttons::BT_OK] =
        std::make_unique<MapleButton>(notice["BtYes"], Point<int16_t>(70, 106));
    buttons_[Buttons::BT_CANCEL] =
        std::make_unique<MapleButton>(notice["BtNo"], Point<int16_t>(130, 106));

    position_ = Point<int16_t>(275, 209);
    dimension_ = Texture(backgrnd).get_dimensions();
}

void UIQuitConfirm::send_key(int32_t keycode, bool pressed, bool escape) {
    if (pressed) {
        if (escape) {
            deactivate();
        } else if (keycode == KeyAction::RETURN) {
            Window::get().terminate();
        }
    }
}

UIElement::Type UIQuitConfirm::get_type() const {
    return TYPE;
}

Button::State UIQuitConfirm::button_pressed(uint16_t buttonid) {
    if (buttonid == BT_OK) {
        Window::get().terminate();
    }

    deactivate();

    return Button::PRESSED;
}
}  // namespace ms
