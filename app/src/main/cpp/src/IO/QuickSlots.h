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

#include <cstdint>
#include <vector>

#include "../Graphics/Texture.h"
#include "KeyAction.h"
#include "Keyboard.h"

namespace ms {
    // The slots the bar along the bottom of the screen shows, and the picture every window
    // draws a function with.
    //
    // The bindings themselves are not stored here. A slot is one key of the committed keymap,
    // which is the only copy that survives a relog because it is the copy the server is told
    // about and given back on the next login. A window that edits bindings stages its own copy
    // and commits when accepted; the bar has nothing to accept, so it commits straight through
    // here.
    class QuickSlots {
    public:
        // The art is a 16x2 grid and every cell of it is a slot. The first twelve are the function
        // keys F1-F12, so they stay reachable from a keyboard; the rest stand for key ids no
        // physical key produces (see keycode), which the keymap carries just the same.
        static constexpr int16_t LENGTH = 32;
        static constexpr int16_t FUNCTION_KEYS = 12;

        // The key a slot stands for. The first slot is the first function key; past F12 the ids
        // continue at 100, clear of the physical keys and of 91/92, which the server keeps for the
        // auto-potion bindings.
        static int32_t keycode(int16_t slot);

        // Whether a key is one of the bar's, which is what lets a window tell a quick slot apart
        // from any other binding when it is handed a key or asked about one.
        static bool is_quick_slot(int32_t keycode);

        // The mapping committed to a slot, or a mapping of NONE when the slot is empty.
        static Keyboard::Mapping at(int16_t slot);

        // Firing a slot. A tap on the bar is sent as the press of the key the slot stands for rather
        // than as the action behind it, so it travels the whole keyboard path and is handled exactly
        // as the same binding pressed on a keyboard would be.
        static void press(int16_t slot, bool pressed);

        // Binding a slot replaces whatever it held, and a mapping of NONE unbinds it. Either way the
        // change goes out in the same packet the keyboard editor sends when its window is accepted,
        // so a binding made by dragging onto the bar lasts exactly as long as one made there.
        //
        // Consumables are the exception to a slot holding whatever it is given: one item occupies one
        // slot, so binding an item that is already bound moves it rather than copying it. The count
        // drawn under the slot is the item's total across every stack in the backpack, which is why a
        // second cell for the same item would only be a second way to drink from one supply.
        static void commit(int16_t slot, Keyboard::Mapping mapping);

        // Dropping the second and later cells of any item that came to be bound in more than one of
        // them. The one-slot rule is newer than the stored keymaps, so a character who bound the same
        // consumable twice keeps both bindings until this runs.
        static void prune_duplicate_items();

        // The functions offered for binding, in the order the binding tray lays them out.
        struct Function {
            KeyAction::Id action;
            // The lettering drawn for this client, when it has any.
            const char *const glyph;
            // The keyboard editor's own icon for it, which is the fallback.
            int16_t index;
        };

        static const std::vector<Function> &tray();

        // The picture a function is drawn with, everywhere a function is drawn. A named glyph comes
        // from the controller sheet, whose lettering was authored for this client; anything without
        // one takes the keyboard editor's art, which is copied into the same sheet under its index.
        static Texture function_icon(KeyAction::Id action);

        // The picture whatever a slot is bound to is drawn with - an item's own art, a skill's, or
        // the function's. Every window that shows a bound slot draws it this way, so a binding looks
        // the same wherever it is read from.
        static Texture icon(Keyboard::Mapping mapping);

        // Which keytype a function has to be dispatched under, so a binding made by dragging behaves
        // exactly like the same one made in the keyboard editor.
        static KeyType::Id function_key_type(KeyAction::Id action);

        // The mapping a tray function stands for, which is what both windows bind with.
        static Keyboard::Mapping function_mapping(KeyAction::Id action);
    };
}  // namespace ms
