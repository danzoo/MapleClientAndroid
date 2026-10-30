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
#include "UILoginWait.h"

#include <imgwz/wz.hpp>
#include <utility>
#define _SILENCE_CXX17_RESULT_OF_DEPRECATION_WARNING
#include "../../../Constants.h"
#include "../../../Graphics/PanelRenderer.h"
#include "../../Net/PacketProcessor.h"
#include "../Components/MapleButton.h"
#include "../UI.h"
#include "UILoginNotice.h"

namespace ms {
UILoginWait::UILoginWait() : UILoginWait([]() {}) {}

UILoginWait::UILoginWait(std::function<void()> okhandler) :
    okhandler_(std::move(okhandler)) {
    img::node Loading = img::wz::ui["Login.img"]["Notice"]["Loading"];
    img::node backgrnd = Loading["backgrnd"];

    sprites_.emplace_back(backgrnd);
    sprites_.emplace_back(Loading["circle"], Point<int16_t>(127, 70));

    buttons_[Buttons::CANCEL] =
        std::make_unique<MapleButton>(Loading["BtCancel"],
                                      Point<int16_t>(101, 106));

    position_ = Point<int16_t>(276, 229);
    dimension_ = Texture(backgrnd).get_dimensions();

    // This box covers the login dialog while it is up, and that dialog is on the panel - the pass
    // that is composited over the interface - so the box belongs there too. It is drawn after the
    // dialog, which puts it on top inside the pass, and it grows about its own centre like the
    // dialog does.
    Point<int16_t> centre =
        position_ + Point<int16_t>(dimension_.x() / 2, dimension_.y() / 2);

    position_ = position_ + Constants::to_panel_space(centre) - centre;
}

void UILoginWait::draw(float inter) const {
    PanelRenderer::get().begin();

    UIElement::draw(inter);

    PanelRenderer::get().end();
}

Cursor::State UILoginWait::send_cursor(bool clicked, Point<int16_t> cursorpos) {
    return UIElement::send_cursor(clicked,
                                  Constants::to_panel_space(cursorpos));
}

bool UILoginWait::is_in_range(Point<int16_t> cursorpos) const {
    return UIElement::is_in_range(Constants::to_panel_space(cursorpos));
}

UIElement::Type UILoginWait::get_type() const {
    return TYPE;
}

void UILoginWait::update() {
    UIElement::update();

    elapsed_ += Constants::TIMESTEP;

    if (elapsed_ < Constants::LOGIN_WAIT_TIMEOUT_MS) {
        return;
    }

    deactivate();

    UI::get().remove(UIElement::Type::LOGIN_NOTICE);
    UI::get().emplace<UILoginNotice>(
        UILoginNotice::Message::UNABLE_TO_CONNECT, okhandler_);
}

void UILoginWait::close() {
    deactivate();
    okhandler_();
}

std::function<void()> UILoginWait::get_handler() {
    return okhandler_;
}

Button::State UILoginWait::button_pressed(uint16_t id) {
    PacketProcessor::get().reconnect();

    close();

    return Button::State::NORMAL;
}
}  // namespace ms