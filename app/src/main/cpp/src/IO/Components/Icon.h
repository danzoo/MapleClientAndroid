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

#include <memory>
#include <optional>

#include "../../Character/Inventory/Inventory.h"
#include "../../Graphics/Texture.h"
#include "../Keyboard.h"

namespace ms {
class Icon {
public:
    enum IconType : uint8_t { NONE, SKILL, EQUIP, ITEM, KEY, GAMEPAD, NUM_TYPES };

    class Type {
    public:
        virtual ~Type() = default;

        virtual void drop_on_stage() const = 0;

        virtual void drop_on_equips(EquipSlot::Id eqslot) const = 0;

        virtual bool drop_on_items(InventoryType::Id tab,
                                   EquipSlot::Id eqslot,
                                   int16_t slot,
                                   bool equip) const = 0;

        virtual void drop_on_bindings(Point<int16_t> cursorposition,
                                      bool remove) const = 0;

        // The binding this icon stands for, when it stands for one. A key, skill or item icon
        // carries its own, so any window that takes bindings can read it without the icon having
        // to know which of them it is being dropped onto.
        virtual std::optional<Keyboard::Mapping> binding() const {
            return std::nullopt;
        }

        // The persistent bar slot this icon was picked up out of, or -1. Wherever such an icon
        // lands that isn't a slot, that slot gives the binding up.
        virtual int16_t quick_slot() const {
            return -1;
        }

        virtual void set_count(int16_t) = 0;

        virtual IconType get_type() = 0;
    };

    class NullType : public Type {
        void drop_on_stage() const override {}
        void drop_on_equips(EquipSlot::Id) const override {}
        bool drop_on_items(InventoryType::Id,
                           EquipSlot::Id,
                           int16_t,
                           bool) const override {
            return true;
        }
        void drop_on_bindings(Point<int16_t>, bool) const override {}
        void set_count(int16_t) override {}
        IconType get_type() override { return IconType::NONE; }
    };

    // An icon that stands for a binding and asks for nothing where it lands. The persistent
    // quick-slot bar shows one of these per bound key and hands it out when it is picked up: every
    // window that takes bindings reads it through binding(), so the icon never has to know which of
    // them it is being dropped onto, and dropping it anywhere else simply leaves it where it was.
    //
    // An icon that came out of a bar slot says which one, and then the rule flips: anywhere that
    // isn't a slot gives that slot back its emptiness, which is how a slot is cleared.
    class Binding : public Type {
    public:
        explicit Binding(Keyboard::Mapping mapping, int16_t slot = -1) :
            mapping_(mapping),
            slot_(slot) {}

        void drop_on_stage() const override;
        void drop_on_equips(EquipSlot::Id) const override;
        bool drop_on_items(InventoryType::Id,
                           EquipSlot::Id,
                           int16_t,
                           bool) const override;
        void drop_on_bindings(Point<int16_t>, bool) const override;
        std::optional<Keyboard::Mapping> binding() const override {
            return mapping_;
        }
        int16_t quick_slot() const override {
            return slot_;
        }
        void set_count(int16_t) override {}
        IconType get_type() override { return IconType::KEY; }

    private:
        Keyboard::Mapping mapping_;
        int16_t slot_;
    };

    Icon(std::unique_ptr<Type> type, Texture texture, int16_t count);

    Icon();

    virtual ~Icon() = default;

    void drop_on_stage() const;
    void drop_on_equips(EquipSlot::Id eqslot) const;
    bool drop_on_items(InventoryType::Id tab,
                       EquipSlot::Id eqslot,
                       int16_t slot,
                       bool equip) const;
    void drop_on_bindings(Point<int16_t> cursorposition, bool remove) const;

    std::optional<Keyboard::Mapping> binding() const;

    int16_t quick_slot() const;

    void set_count(int16_t count);

    IconType get_type();

    void draw(Point<int16_t> position) const;

    void dragdraw(Point<int16_t> cursorpos) const;

    void start_drag(Point<int16_t> offset);

    void reset();

    virtual Texture get_texture() const;

    int16_t get_count() const;

    bool get_drag();

private:
    std::unique_ptr<Type> type_;
    bool show_count_;
    int16_t count_;

    Texture texture_;
    bool dragged_;
    Point<int16_t> cursor_offset_;
};
}  // namespace ms