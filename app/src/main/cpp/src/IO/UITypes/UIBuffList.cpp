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
#include "UIBuffList.h"

#include <imgwz/node.hpp>
#include <imgwz/wz.hpp>

#include "../../Gameplay/Stage.h"
#include "../Data/ItemData.h"
#include "../UI.h"
#include "StringHandling.h"

namespace ms {
BuffIcon::BuffIcon(int32_t buff, int32_t dur) :
    cover_(IconCover::BUFF, dur - FLASH_TIME_) {
    buffid_ = buff;
    duration_ = dur;
    opacity_.set(1.0f);
    // The blink is a rate per second rather than an amount per update, and the sign is what says
    // which way it is going.
    opcstep_ = -Constants::BUFF_FADE_PER_SECOND;

    if (buffid_ >= 0) {
        std::string strid = string_format::extend_id(buffid_, 7);
        img::node src =
            img::wz::skill[strid.substr(0, 3) + ".img"]["skill"][strid];
        icon_ = src["icon"];
    } else {
        icon_ = ItemData::get(-buffid_).get_icon(true);

        // The bottle alone read as a missing frame: skill buffs carry their frame baked into the
        // icon, item icons do not. Seat the bottle in the empty buff slot (BuffIcon.img's generic
        // frame, cut from a skill icon's border) so both kinds read the same.
        base_ = img::wz::ui["BuffIcon.img"]["united"]["buff"]["0"];
        has_base_ = base_.is_valid();

        if (has_base_) {
            const Point<int16_t> bdim = base_.get_dimensions();
            const Point<int16_t> idim = icon_.get_dimensions();
            icon_offset_ = icon_.get_origin() - base_.get_origin()
                + Point<int16_t>((bdim.x() - idim.x()) / 2, (bdim.y() - idim.y()) / 2);
        }
    }
}

void BuffIcon::draw(Point<int16_t> position, float alpha) const {
    const float opacity = opacity_.get(alpha);

    if (has_base_) {
        base_.draw(DrawArgument(position, opacity));
    }
    icon_.draw(DrawArgument(position + icon_offset_, opacity));
    cover_.draw(position + Point<int16_t>(1, -31), alpha);
}

bool BuffIcon::update() {
    if (duration_ <= FLASH_TIME_) {
        opacity_ += opcstep_ * Constants::TICK_SECONDS;

        bool fadedout = opcstep_ < 0.0f && opacity_.last() <= 0.0f;
        bool fadedin = opcstep_ > 0.0f && opacity_.last() >= 1.0f;

        if (fadedout || fadedin) {
            opcstep_ = -opcstep_;
        }
    }

    cover_.update();

    duration_ -= Constants::TIMESTEP;

    return duration_ < Constants::TIMESTEP;
}

UIBuffList::UIBuffList() {
    int16_t height = Constants::Constants::get().get_ui_viewheight();
    int16_t width = Constants::Constants::get().get_ui_viewwidth();

    update_screen(width, height);
}

void UIBuffList::draw(float alpha) const {
    Point<int16_t> icpos = position_;

    for (const auto &icon : icons_) {
        icon.second.draw(icpos, alpha);
        icpos.shift_x(-32);
    }
}

void UIBuffList::update() {
    for (auto iter = icons_.begin(); iter != icons_.end();) {
        bool expired = iter->second.update();

        if (expired) {
            iter = icons_.erase(iter);
        } else {
            iter++;
        }
    }

    if (tooltip_buffid_ && icons_.find(tooltip_buffid_) == icons_.end()) {
        UI::get().clear_tooltip(Tooltip::Parent::BUFF_LIST);
        tooltip_buffid_ = 0;
    }
}

void UIBuffList::update_screen(int16_t new_width, int16_t) {
    position_ = Point<int16_t>(new_width - 35, 55);
    dimension_ = Point<int16_t>(position_.x(), 32);
}

Rectangle<int16_t> UIBuffList::row_bounds() const {
    const int16_t count = static_cast<int16_t>(icons_.size());

    return Rectangle<int16_t>(position_.x() - 32 * (count - 1), position_.x() + 32,
                              position_.y() - 32, position_.y());
}

bool UIBuffList::is_in_range(Point<int16_t> cursorpos) const {
    return row_bounds().contains(cursorpos);
}

Cursor::State UIBuffList::send_cursor(bool pressed,
                                      Point<int16_t> cursorposition) {
    if (!pressed) {
        return UIElement::send_cursor(pressed, cursorposition);
    }

    // Same walk as draw(), so the cell that took the tap is the cell that was painted there.
    Point<int16_t> icpos = position_;

    for (const auto &icon : icons_) {
        const Rectangle<int16_t> cell(icpos.x(), icpos.x() + 32,
                                      icpos.y() - 32, icpos.y());

        if (cell.contains(cursorposition)) {
            const int32_t buffid = icon.first;
            const Point<int16_t> anchor = icpos + Point<int16_t>(16, -16);

            if (buffid >= 0) {
                const SkillBook &book = Stage::get().get_player().get_skills();

                UI::get().show_skill(Tooltip::Parent::BUFF_LIST,
                                     buffid,
                                     book.get_level(buffid),
                                     book.get_masterlevel(buffid),
                                     0,
                                     anchor);
            } else {
                UI::get().show_item(Tooltip::Parent::BUFF_LIST, -buffid, anchor);
            }

            tooltip_buffid_ = buffid;

            return Cursor::State::IDLE;
        }

        icpos.shift_x(-32);
    }

    return Cursor::State::IDLE;
}

UIElement::Type UIBuffList::get_type() const {
    return TYPE;
}

void UIBuffList::add_buff(int32_t buffid, int32_t duration) {
    // A refreshed buff (a second potion, a map change re-send) restarts the countdown instead of
    // keeping the old icon's remaining time.
    icons_.insert_or_assign(buffid, BuffIcon(buffid, duration));
}
}  // namespace ms