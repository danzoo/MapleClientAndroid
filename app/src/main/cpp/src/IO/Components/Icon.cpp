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
#include "Icon.h"

#include <imgwz/wz.hpp>

#include "../../Audio/Audio.h"
#include "../QuickSlots.h"
#include "Charset.h"

namespace ms {
// An icon picked up out of the persistent bar leaves that bar empty wherever it lands that isn't
// another bar slot: the slot it came from gives the binding back, which is how a slot is cleared
// without a second control for it.
void Icon::Binding::drop_on_stage() const {
    if (slot_ >= 0) {
        QuickSlots::commit(slot_, Keyboard::Mapping(KeyType::Id::NONE, 0));
    }
}

void Icon::Binding::drop_on_equips(EquipSlot::Id) const {
    drop_on_stage();
}

bool Icon::Binding::drop_on_items(InventoryType::Id,
                                  EquipSlot::Id,
                                  int16_t,
                                  bool) const {
    drop_on_stage();
    return true;
}

void Icon::Binding::drop_on_bindings(Point<int16_t>, bool) const {
    drop_on_stage();
}

Icon::Icon(std::unique_ptr<Type> t, Texture tx, int16_t c) :
    type_(std::move(t)),
    show_count_(c > -1),
    count_(c),
    texture_(tx),
    dragged_(false) {
    texture_.shift(Point<int16_t>(0, 32));
}

Icon::Icon() : Icon(std::make_unique<NullType>(), {}, -1) {}

void Icon::draw(Point<int16_t> position) const {
    float opacity = dragged_ ? 0.5f : 1.0f;
    get_texture().draw(DrawArgument(position, opacity));

    if (show_count_) {
        static const Charset countset =
            Charset(img::wz::ui["Basic.img"]["ItemNo"],
                    Charset::Alignment::LEFT);
        countset.draw(std::to_string(count_), position + Point<int16_t>(0, 20));
    }
}

void Icon::dragdraw(Point<int16_t> cursorpos) const {
    if (dragged_ && texture_.is_valid()) {
        texture_.draw(DrawArgument(cursorpos - cursor_offset_, 0.5f));
    }
}

void Icon::drop_on_stage() const {
    type_->drop_on_stage();
}

void Icon::drop_on_equips(EquipSlot::Id eqslot) const {
    type_->drop_on_equips(eqslot);
}

bool Icon::drop_on_items(InventoryType::Id tab,
                         EquipSlot::Id eqslot,
                         int16_t slot,
                         bool equip) const {
    if (!texture_.is_valid()) {
        return false;
    }

    bool remove_icon = false;
    if (type_) {
        remove_icon = type_->drop_on_items(tab, eqslot, slot, equip);
    }

    if (remove_icon) {
        Sound(Sound::Name::DRAG_END).play();
    }

    return remove_icon;
}

void Icon::drop_on_bindings(Point<int16_t> cursorposition, bool remove) const {
    type_->drop_on_bindings(cursorposition, remove);
}

std::optional<Keyboard::Mapping> Icon::binding() const {
    return type_->binding();
}

int16_t Icon::quick_slot() const {
    return type_->quick_slot();
}

void Icon::start_drag(Point<int16_t> offset) {
    cursor_offset_ = offset;
    dragged_ = true;

    Sound(Sound::Name::DRAG_START).play();
}

void Icon::reset() {
    dragged_ = false;
}

// Allows for Icon extensibility
// Use this instead of referencing texture directly
Texture Icon::get_texture() const {
    return texture_;
}

void Icon::set_count(int16_t c) {
    count_ = c;
    type_->set_count(c);
}

Icon::IconType Icon::get_type() {
    return type_->get_type();
}

int16_t Icon::get_count() const {
    return count_;
}

bool Icon::get_drag() {
    return dragged_;
}
}  // namespace ms