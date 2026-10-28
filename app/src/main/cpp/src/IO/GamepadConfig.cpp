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
#include "GamepadConfig.h"

#include <sstream>
#include <string>

#include "../Configuration.h"
#include "../Util/StringHandling.h"
#include "UI.h"

namespace ms {
    namespace {
        // Where each slot sits on the 620x231 controller, measured off the art.
        constexpr Point<int16_t> kPositions[GamepadConfig::Slot::LENGTH] = {
            {94, 17},   // DPAD_UP
            {127, 50},  // DPAD_RIGHT
            {94, 83},   // DPAD_DOWN
            {61, 50},   // DPAD_LEFT
            {245, 46},  // L1
            {278, 46},  // L2
            {311, 46},  // R2
            {344, 46},  // R1
            {245, 94},  // START
            {344, 94},  // SELECT
            {498, 17},  // Y
            {465, 50},  // X
            {531, 50},  // B
            {498, 83},  // A
            {94, 125},  // LS_UP
            {127, 158}, // LS_RIGHT
            {94, 191},  // LS_DOWN
            {61, 158},  // LS_LEFT
            {94, 158},  // LS_PRESS
            {498, 125}, // RS_UP
            {531, 158}, // RS_RIGHT
            {498, 191}, // RS_DOWN
            {465, 158}, // RS_LEFT
            {498, 158}, // RS_PRESS
        };

        const char *const kNames[GamepadConfig::Slot::LENGTH] = {
            "D-Pad Up",
            "D-Pad Right",
            "D-Pad Down",
            "D-Pad Left",
            "L1",
            "L2",
            "R2",
            "R1",
            "Start",
            "Select",
            "Y",
            "X",
            "B",
            "A",
            "Left Stick Up",
            "Left Stick Right",
            "Left Stick Down",
            "Left Stick Left",
            "Left Stick Press",
            "Right Stick Up",
            "Right Stick Right",
            "Right Stick Down",
            "Right Stick Left",
            "Right Stick Press",
        };
    }  // namespace

    GamepadConfig::GamepadConfig() {
        for (uint8_t slot = 0; slot < Slot::LENGTH; ++slot) {
            mappings_[slot] = default_mapping(static_cast<Slot>(slot));
        }

        load();
        apply();
    }

    int32_t GamepadConfig::keycode(Slot slot) {
        return VIRTUAL_KEY + slot;
    }

    Point<int16_t> GamepadConfig::position(Slot slot) {
        return kPositions[slot];
    }

    const char *GamepadConfig::name(Slot slot) {
        return kNames[slot];
    }

    Keyboard::Mapping GamepadConfig::default_mapping(Slot slot) {
        using Action = KeyAction::Id;

        switch (slot) {
            case Slot::DPAD_UP:
            case Slot::LS_UP:
                return {KeyType::Id::ACTION, Action::UP};
            case Slot::DPAD_RIGHT:
            case Slot::LS_RIGHT:
                return {KeyType::Id::ACTION, Action::RIGHT};
            case Slot::DPAD_DOWN:
            case Slot::LS_DOWN:
                return {KeyType::Id::ACTION, Action::DOWN};
            case Slot::DPAD_LEFT:
            case Slot::LS_LEFT:
                return {KeyType::Id::ACTION, Action::LEFT};
            case Slot::L1:
                return {KeyType::Id::MENU, Action::ITEMS};
            case Slot::L2:
                return {KeyType::Id::MENU, Action::STATS};
            case Slot::R2:
                return {KeyType::Id::MENU, Action::QUICK_SLOTS};
            case Slot::R1:
                return {KeyType::Id::MENU, Action::SKILLS};
            case Slot::START:
                return {KeyType::Id::MENU, Action::MENU};
            case Slot::SELECT:
                return {KeyType::Id::MENU, Action::MAIN_MENU};
            case Slot::Y:
                return {KeyType::Id::ACTION, Action::PICKUP};
            case Slot::X:
                return {KeyType::Id::MENU, Action::INTERACT_HARVEST};
            case Slot::B:
                return {KeyType::Id::ACTION, Action::ATTACK};
            case Slot::A:
                return {KeyType::Id::ACTION, Action::JUMP};
            case Slot::LS_PRESS:
                return {KeyType::Id::MENU, Action::WORLD_MAP};
            case Slot::RS_PRESS:
                return {KeyType::Id::MENU, Action::MINI_MAP};
            case Slot::RS_UP:
            case Slot::RS_RIGHT:
            case Slot::RS_DOWN:
            case Slot::RS_LEFT:
            default:
                return {};
        }
    }

    Keyboard::Mapping GamepadConfig::mapping(Slot slot) const {
        return mappings_[slot];
    }

    const std::array<Keyboard::Mapping, GamepadConfig::Slot::LENGTH> &
    GamepadConfig::mappings() const {
        return mappings_;
    }

    void GamepadConfig::commit(const std::array<Keyboard::Mapping, Slot::LENGTH> &mappings) {
        mappings_ = mappings;
        apply();

        std::string stored;

        for (uint8_t slot = 0; slot < Slot::LENGTH; ++slot) {
            const Keyboard::Mapping &mapping = mappings_[slot];

            // Only what the player changed is written, so an empty value means the defaults and a
            // setting left from a build that had fewer slots still parses.
            if (mapping == default_mapping(static_cast<Slot>(slot))) {
                continue;
            }

            std::ostringstream entry;
            entry << static_cast<int32_t>(mapping.type) << ',' << mapping.action;
            stored += std::to_string(slot) + '=' + entry.str() + ';';
        }

        Setting<GamepadBindings>::get().save(stored);
        Configuration::get().save();
    }

    void GamepadConfig::load() {
        const std::string stored = Setting<GamepadBindings>::get().load();

        std::istringstream stream(stored);
        std::string pair;

        while (std::getline(stream, pair, ';')) {
            const size_t equals = pair.find('=');

            if (equals == std::string::npos) {
                continue;
            }

            int32_t slot = string_conversion::or_zero<int32_t>(pair.substr(0, equals));
            std::istringstream body(pair.substr(equals + 1));

            int32_t type = 0;
            int32_t action = 0;
            if (!(body >> type)) {
                continue;
            }
            body.ignore(1);
            body >> action;

            if (slot < 0 || slot >= Slot::LENGTH || type < 0 || type >= KeyType::LENGTH) {
                continue;
            }

            mappings_[slot] = Keyboard::Mapping(static_cast<KeyType::Id>(type), action);
        }
    }

    void GamepadConfig::apply() const {
        Keyboard &keyboard = UI::get().get_keyboard();

        for (uint8_t slot = 0; slot < Slot::LENGTH; ++slot) {
            keyboard.map_virtual(keycode(static_cast<Slot>(slot)),
                                 mappings_[slot]);
        }
    }
}  // namespace ms
