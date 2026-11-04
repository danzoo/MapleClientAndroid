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
#include "UIElement.h"

#include "../Audio/Audio.h"

namespace ms {
UIElement::UIElement(Point<int16_t> p, Point<int16_t> d, bool a) :
    position_(p),
    dimension_(d),
    active_(a) {}

UIElement::UIElement(Point<int16_t> p, Point<int16_t> d) :
    UIElement(p, d, true) {}

UIElement::UIElement() : UIElement(Point<int16_t>(), Point<int16_t>()) {}

void UIElement::draw(float alpha) const {
    draw_sprites(alpha);
    draw_buttons(alpha);
}

void UIElement::draw_sprites(float alpha) const {
    for (const Sprite &sprite : sprites_) {
        sprite.draw(position_, alpha);
    }
}

void UIElement::draw_buttons(float) const {
    for (const auto &iter : buttons_) {
        if (const Button *button = iter.second.get()) {
            button->draw(position_);
        }
    }
}

void UIElement::update() {
    for (auto &sprite : sprites_) {
        sprite.update();
    }

    for (auto &iter : buttons_) {
        if (Button *button = iter.second.get()) {
            button->update();
        }
    }
}

void UIElement::makeactive() {
    active_ = true;
}

void UIElement::deactivate() {
    active_ = false;
}

bool UIElement::is_active() const {
    return active_;
}

void UIElement::toggle_active() {
    if (active_) {
        deactivate();
    } else {
        makeactive();
    }
}

bool UIElement::is_in_range(Point<int16_t> cursorpos) const {
    auto bounds = Rectangle<int16_t>(position_, position_ + dimension_);

    return bounds.contains(cursorpos);
}

bool UIElement::button_at(Point<int16_t> pos, uint16_t &buttonid) const {
    constexpr int16_t TOUCH_PAD = 6;

    buttonid = 0;
    bool exact = false;

    for (auto &btit : buttons_) {
        auto *button = btit.second.get();
        if (!button->is_active()) {
            continue;
        }

        // A greyed button never armed under the old hover protocol, so a press could not fire it
        // either. With the arming step gone, the match has to refuse it here or taps would start
        // reaching disabled buttons.
        if (button->get_state() == Button::State::DISABLED) {
            continue;
        }

        Rectangle<int16_t> b = button->bounds(position_);
        if (b.contains(pos)) {
            buttonid = btit.first;
            exact = true;
            break;
        }

        Rectangle<int16_t> padded(b.get_left_top() - TOUCH_PAD,
                                  b.get_right_bottom() + TOUCH_PAD);
        if (padded.contains(pos)) {
            // Remembered, not returned: a later button that the press lands on exactly still
            // wins, because overlapping sprites should prefer the one actually under the finger.
            if (buttonid == 0) {
                buttonid = btit.first;
            }
        }
    }

    return exact || buttonid != 0;
}

// Two different things used to share this call, because the PC client drove its interface with a
// pointer that was always somewhere: the pass with down == false painted the hover look and armed
// the button, and only a press that arrived on an already-armed button fired it. On touch the
// finger is nowhere between gestures, so the arming left every button wearing its hover art, and
// a window reopened under the last touch position re-raised whatever had been hovered there.
//
// The two roles are separate now. down == false is a pure hit query: it only reports what the
// touch layer needs to know about the spot (can this be clicked, grabbed, dragged, scrolled). It
// changes no button state, plays no sound and raises no preview. down == true is the whole action:
// a press that lands on a button fires it, because the hit query just said which one it is.
Cursor::State UIElement::send_cursor(bool down, Point<int16_t> pos) {
    uint16_t matched = 0;

    if (!button_at(pos, matched)) {
        return down ? Cursor::State::CLICKING : Cursor::State::IDLE;
    }

    if (!down) {
        return Cursor::State::CAN_CLICK;
    }

    Sound(Sound::Name::BUTTON_CLICK).play();
    buttons_[matched]->set_state(button_pressed(matched));

    return Cursor::State::IDLE;
}
}  // namespace ms