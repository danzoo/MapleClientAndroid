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
#include "QuickSlots.h"

#include <imgwz/wz.hpp>
#include <tuple>
#include <utility>
#include <vector>

#include "../../Net/Packets/PlayerPackets.h"
#include "../Data/ItemData.h"
#include "../Data/SkillData.h"
#include "KeyConfig.h"
#include "UI.h"

namespace ms {
    namespace {
        // The functions offered for binding, and what each is drawn with. One icon of this list is
        // what a binding tray hands out; a function whose lettering was drawn in Chinese for this
        // client is named in the controller sheet and takes its glyph from there, and every other
        // one takes the keyboard editor's art, copied into the same sheet under its index there.
        const std::vector<QuickSlots::Function> kTray = {
            {KeyAction::Id::EQUIPMENT, "equipment", 0},
            {KeyAction::Id::ITEMS, "items", 1},
            {KeyAction::Id::STATS, "stats", 2},
            {KeyAction::Id::SKILLS, "skills", 3},
            {KeyAction::Id::FRIENDS, "friends", 4},
            {KeyAction::Id::WORLD_MAP, "worldmap", 5},
            {KeyAction::Id::MAPLE_CHAT, "maplechat", 6},
            {KeyAction::Id::MINI_MAP, "minimap", 7},
            {KeyAction::Id::QUEST_LOG, "questlog", 8},
            {KeyAction::Id::KEY_BINDINGS, "keybindings", 9},
            {KeyAction::Id::SAY, "say", 10},
            {KeyAction::Id::WHISPER, "whisper", 11},
            {KeyAction::Id::PARTY_CHAT, "partychat", 12},
            {KeyAction::Id::FRIENDS_CHAT, "friendschat", 13},
            {KeyAction::Id::MENU, "menu", 14},
            {KeyAction::Id::QUICK_SLOTS, "quickslots", 15},
            {KeyAction::Id::TOGGLE_CHAT, "togglechat", 16},
            {KeyAction::Id::GUILD, "guild", 17},
            {KeyAction::Id::GUILD_CHAT, "guildchat", 18},
            {KeyAction::Id::PARTY, "party", 19},
            {KeyAction::Id::NOTIFIER, "notifier", 20},
            {KeyAction::Id::MAPLE_NEWS, nullptr, 21},
            {KeyAction::Id::CASHSHOP, "cashshop", 22},
            {KeyAction::Id::ALLIANCE_CHAT, "alliancechat", 23},
            {KeyAction::Id::MANAGE_LEGION, nullptr, 25},
            {KeyAction::Id::MEDALS, "medals", 26},
            {KeyAction::Id::BOSS_PARTY, "bossparty", 27},
            {KeyAction::Id::PROFESSION, "profession", 29},
            {KeyAction::Id::ITEM_POT, nullptr, 30},
            {KeyAction::Id::EVENT, nullptr, 31},
            {KeyAction::Id::SILENT_CRUSADE, nullptr, 33},
            {KeyAction::Id::BATTLE_ANALYSIS, nullptr, 35},
            {KeyAction::Id::GUIDE, nullptr, 39},
            {KeyAction::Id::ENHANCE_EQUIP, nullptr, 41},
            {KeyAction::Id::MONSTER_COLLECTION, nullptr, 42},
            {KeyAction::Id::SOUL_WEAPON, nullptr, 43},
            {KeyAction::Id::CHAR_INFO, nullptr, 44},
            {KeyAction::Id::CHANGE_CHANNEL, nullptr, 45},
            {KeyAction::Id::MAIN_MENU, nullptr, 46},
            {KeyAction::Id::PICTURE_MODE, nullptr, 47},
            {KeyAction::Id::MAPLE_ACHIEVEMENT, nullptr, 48},
            {KeyAction::Id::PICKUP, "pickup", 49},
            {KeyAction::Id::SIT, "sit", 50},
            {KeyAction::Id::ATTACK, "attack", 51},
            {KeyAction::Id::JUMP, "jump", 52},
            {KeyAction::Id::INTERACT_HARVEST, "interact", 53},
            {KeyAction::Id::LEFT, "left", -1},
            {KeyAction::Id::RIGHT, "right", -1},
            {KeyAction::Id::UP, "up", -1},
            {KeyAction::Id::DOWN, "down", -1},
            {KeyAction::Id::FACE1, nullptr, 100},
            {KeyAction::Id::FACE2, nullptr, 101},
            {KeyAction::Id::FACE3, nullptr, 102},
            {KeyAction::Id::FACE4, nullptr, 103},
            {KeyAction::Id::FACE5, nullptr, 104},
            {KeyAction::Id::FACE6, nullptr, 105},
            {KeyAction::Id::FACE7, nullptr, 106},
            {KeyAction::Id::MUTE, nullptr, 202},
            {KeyAction::Id::MONSTER_BOOK, "monsterbook", 1000},
            {KeyAction::Id::TO_SPOUSE, "tospouse", 1001},
        };

        auto fn_change_keymap = []<typename... T>(T &&...args) {
            ChangeKeyMapPacket(std::forward<T>(args)...).dispatch();
        };

        // Applying a set of slot changes at once, and telling the server about all of them in the one
        // packet the keyboard editor sends when its window is accepted with several bindings moved.
        void apply(const std::vector<std::pair<int16_t, Keyboard::Mapping>> &changes) {
            if (changes.empty()) {
                return;
            }

            Keyboard &keyboard = UI::get().get_keyboard();
            std::vector<std::tuple<KeyConfig::Key, KeyType::Id, int32_t>> updated;

            for (const std::pair<int16_t, Keyboard::Mapping> &change: changes) {
                const int32_t key = QuickSlots::keycode(change.first);
                const Keyboard::Mapping &mapping = change.second;

                if (mapping.type == KeyType::Id::NONE) {
                    keyboard.remove(key);
                }
                else {
                    keyboard.assign(key,
                                    static_cast<uint8_t>(mapping.type),
                                    mapping.action);
                }

                updated.emplace_back(KeyConfig::actionbyid(key),
                                     mapping.type,
                                     mapping.action);
            }

            fn_change_keymap(updated);
        }
    }  // namespace

    int32_t QuickSlots::keycode(int16_t slot) {
        return VIRTUAL_BASE + slot;
    }

    Keyboard::Mapping QuickSlots::at(int16_t slot) {
        return UI::get().get_keyboard().get_maple_mapping(keycode(slot));
    }

    void QuickSlots::press(int16_t slot, bool pressed) {
        Keyboard &keyboard = UI::get().get_keyboard();

        UI::get().send_key(keyboard.get_keycode(
                               static_cast<uint8_t>(keycode(slot))),
                           pressed);
    }

    void QuickSlots::commit(int16_t slot, Keyboard::Mapping mapping) {
        std::vector<std::pair<int16_t, Keyboard::Mapping>> changes;
        changes.emplace_back(slot, mapping);

        // One consumable occupies one slot: binding an item that is already bound somewhere else
        // moves it out of that slot instead of leaving two cells that both drink from the same
        // supply.
        if (mapping.type == KeyType::Id::ITEM) {
            for (int16_t other = 0; other < LENGTH; ++other) {
                if (other != slot && at(other) == mapping) {
                    changes.emplace_back(other,
                                         Keyboard::Mapping(KeyType::Id::NONE, 0));
                }
            }
        }

        apply(changes);
    }

    void QuickSlots::prune_duplicate_items() {
        // The rule is only as old as this file, so a character's stored keymap can still hold the
        // same consumable in several slots; the first cell to hold it keeps it and the rest give it
        // up, which is what an item is bound to once.
        std::vector<std::pair<int16_t, Keyboard::Mapping>> changes;

        for (int16_t slot = 0; slot < LENGTH; ++slot) {
            const Keyboard::Mapping mapping = at(slot);

            if (mapping.type != KeyType::Id::ITEM) {
                continue;
            }

            for (int16_t first = 0; first < slot; ++first) {
                if (at(first) == mapping) {
                    changes.emplace_back(slot,
                                         Keyboard::Mapping(KeyType::Id::NONE, 0));
                    break;
                }
            }
        }

        apply(changes);
    }

    const std::vector<QuickSlots::Function> &QuickSlots::tray() {
        return kTray;
    }

    Texture QuickSlots::function_icon(KeyAction::Id action) {
        for (const Function &function: kTray) {
            if (function.action != action) {
                continue;
            }

            if (function.glyph) {
                return img::wz::ui["Controller.img"]["icon"][function.glyph];
            }

            return img::wz::ui["Controller.img"]["icon"][function.index];
        }

        return Texture();
    }

    Texture QuickSlots::icon(Keyboard::Mapping mapping) {
        switch (mapping.type) {
            case KeyType::Id::ITEM:
                return ItemData::get(mapping.action).get_icon(false);
            case KeyType::Id::SKILL:
                return SkillData::get(mapping.action).get_icon(
                    SkillData::Icon::NORMAL);
            case KeyType::Id::MENU:
            case KeyType::Id::ACTION:
            case KeyType::Id::FACE:
                return function_icon(
                    static_cast<KeyAction::Id>(mapping.action));
            default:
                return Texture();
        }
    }

    KeyType::Id QuickSlots::function_key_type(KeyAction::Id action) {
        switch (action) {
            case KeyAction::Id::PICKUP:
            case KeyAction::Id::SIT:
            case KeyAction::Id::ATTACK:
            case KeyAction::Id::JUMP:
            case KeyAction::Id::LEFT:
            case KeyAction::Id::RIGHT:
            case KeyAction::Id::UP:
            case KeyAction::Id::DOWN:
                return KeyType::Id::ACTION;
            case KeyAction::Id::FACE1:
            case KeyAction::Id::FACE2:
            case KeyAction::Id::FACE3:
            case KeyAction::Id::FACE4:
            case KeyAction::Id::FACE5:
            case KeyAction::Id::FACE6:
            case KeyAction::Id::FACE7:
                return KeyType::Id::FACE;
            default:
                return KeyType::Id::MENU;
        }
    }

    Keyboard::Mapping QuickSlots::function_mapping(KeyAction::Id action) {
        return Keyboard::Mapping(function_key_type(action), action);
    }
}  // namespace ms
