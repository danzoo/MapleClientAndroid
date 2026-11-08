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

#include <array>
#include <cstdint>

#include "Keyboard.h"
#include "Point.h"
#include "Singleton.h"

namespace ms {
    // Every place on a controller that a function can be bound to, the code each one is sent
    // under, where it sits on the art and what it does before the player changes it. The binding
    // window edits a copy of this and hands the whole table back on OK; the rest of the client
    // only ever reads the committed one.
    class GamepadConfig : public Singleton<GamepadConfig> {
    public:
        // Ordered the way the controller is drawn, so the table reads as the picture does.
        enum Slot : uint8_t {
            DPAD_UP,
            DPAD_RIGHT,
            DPAD_DOWN,
            DPAD_LEFT,
            L1,
            L2,
            R2,
            R1,
            START,
            SELECT,
            Y,
            X,
            B,
            A,
            LS_UP,
            LS_RIGHT,
            LS_DOWN,
            LS_LEFT,
            LS_PRESS,
            RS_UP,
            RS_RIGHT,
            RS_DOWN,
            RS_LEFT,
            RS_PRESS,
            LENGTH
        };

        // A binding is drawn into the square the art leaves for it on the controller.
        static constexpr int16_t SLOT_SIZE = 32;
        // Codes no keyboard produces. A slot is dispatched through the ordinary key path under
        // its own code, so dialogs, menus and the chat bar treat a controller press exactly like
        // the key the binding stands for.
        static constexpr int32_t VIRTUAL_KEY = 0x200;
        // The Maple key ids the table travels to the server under. Ids 0-99 are the physical
        // keyboard, 100-131 the quick-slot bar's cells and 132-159 are the rest of what the keymap
        // packet reaches, so the 24 controller slots go there: the server already stores that
        // packet per character, which is what makes the bindings belong to one character instead
        // of to the device.
        static constexpr uint8_t FIRST_KEY = 132;

        static_assert(FIRST_KEY + Slot::LENGTH <= 160,
                      "the controller table has to fit inside the keymap packet");

        GamepadConfig();

        static int32_t keycode(Slot slot);

        // Top-left of the slot in controller-art space.
        static Point<int16_t> position(Slot slot);

        // What a slot holds for a character that has never bound it. A slot left at this never
        // reaches the server as a row, and a row the server does not send back is read as this.
        static Keyboard::Mapping default_mapping(Slot slot);

        // The name shown under the slot in the binding window.
        static const char *name(Slot slot);

        Keyboard::Mapping mapping(Slot slot) const;

        const std::array<Keyboard::Mapping, Slot::LENGTH> &mappings() const;

        // Install a whole table as the live one, keep a copy in the settings file as what this
        // device uses before a character is known, and send it to the server as this character's.
        void commit(const std::array<Keyboard::Mapping, Slot::LENGTH> &mappings);

        // One slot arriving in the keymap packet, addressed by its Maple key id rather than by slot
        // so the caller does not have to know the range. A type of zero means the server holds no
        // row for this character here, which is the slot's own default rather than another
        // character's setting: the packet is positional, so this is where a character that has
        // bound nothing of its own stops inheriting whoever bound the slot last.
        void assign_slot(int32_t key, uint8_t type, int32_t action);

    private:
        void load();
        void apply() const;

        std::array<Keyboard::Mapping, Slot::LENGTH> mappings_;
    };
}  // namespace ms
