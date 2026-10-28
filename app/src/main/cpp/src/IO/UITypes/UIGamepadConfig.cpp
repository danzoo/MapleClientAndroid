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
#include "UIGamepadConfig.h"

#include <imgwz/wz.hpp>
#include <string>
#include <utility>

#include "../../Constants.h"
#include "../../Data/ItemData.h"
#include "../../Data/SkillData.h"
#include "../Components/MapleButton.h"
#include "../GamepadConfig.h"
#include "../UI.h"
#include "UINotice.h"

namespace ms {
    namespace {
        // The controller art is laid out inside the keyboard editor's panel, in the same place the
        // keyboard picture sits, so the two windows read as one family.
        constexpr Point<int16_t> kPlateOrigin(20, 67);
        constexpr Point<int16_t> kTrayOrigin(26, 307);
        constexpr int16_t kTrayColumns = 17;
        constexpr int16_t kTrayPitch = 36;

        // Which keytype a function needs to be recognised as, copied from the keyboard editor so a
        // binding made here behaves exactly like the same one made there.
        KeyType::Id key_type(KeyAction::Id action) {
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

        // One icon of the tray. A function whose lettering was drawn in Chinese for this window is
        // named in the controller sheet and takes its glyph from there; every other one borrows the
        // keyboard editor's own art by index, which is also the fallback if the named glyph is
        // missing from the file.
        struct TraySpec {
            KeyAction::Id action;
            const char *const glyph;
            int16_t index;
        };

        const TraySpec kTray[] = {
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
            {KeyAction::Id::SCREENSHOT, nullptr, 47},
            {KeyAction::Id::PICTURE_MODE, nullptr, 48},
            {KeyAction::Id::MAPLE_ACHIEVEMENT, nullptr, 49},
            {KeyAction::Id::PICKUP, "pickup", 50},
            {KeyAction::Id::SIT, "sit", 51},
            {KeyAction::Id::ATTACK, "attack", 52},
            {KeyAction::Id::JUMP, "jump", 53},
            {KeyAction::Id::INTERACT_HARVEST, "interact", 54},
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
    }  // namespace

    UIGamepadConfig::UIGamepadConfig(const Inventory &in_inventory,
                                     const SkillBook &in_skillbook) :
            UIDragElement<PosGAMEPADCONFIG>(),
            inventory_(in_inventory),
            skillbook_(in_skillbook),
            dirty_(false),
            grabbed_(GamepadConfig::Slot::LENGTH) {
        img::node KeyConfig = img::wz::ui["StatusBar3.img"]["KeyConfig"];
        img::node Controller = img::wz::ui["Controller.img"];

        action_icon_ = KeyConfig["icon"];
        tray_icon_ = Controller["icon"];

        img::node backgrnd = KeyConfig["backgrnd"];
        Texture bg = backgrnd;
        dimension_ = bg.get_dimensions();

        sprites_.emplace_back(backgrnd);
        sprites_.emplace_back(KeyConfig["backgrnd2"]);
        sprites_.emplace_back(Controller["backgrnd"], DrawArgument(kPlateOrigin));

        // The plate's key labels belong on top of the bound icons, not under them, so they are not
        // one of the background sprites.
        key_overlay_ = Controller["key"];

        img::node BtClose3 = img::wz::ui["Basic.img"]["BtClose3"];
        img::node buttons = Controller["button"];

        buttons_[Buttons::CLOSE] = std::make_unique<MapleButton>(
                BtClose3,
                Point<int16_t>(dimension_.x() - 18, 3));
        buttons_[Buttons::DEFAULT] =
                std::make_unique<MapleButton>(buttons["Default"],
                                              Point<int16_t>(20, 460));
        buttons_[Buttons::DELETE] =
                std::make_unique<MapleButton>(buttons["Delete"],
                                              Point<int16_t>(83, 460));
        buttons_[Buttons::OK] = std::make_unique<MapleButton>(buttons["OK"],
                                                              Point<int16_t>(536,
                                                                            460));
        buttons_[Buttons::CANCEL] =
                std::make_unique<MapleButton>(buttons["Cancel"],
                                              Point<int16_t>(582, 460));

        drag_area_ = Point<int16_t>(dimension_.x(), 20);

        load_tray();
        reset();
    }

    void UIGamepadConfig::load_tray() {
        for (const TraySpec &spec: kTray) {
            // Every glyph in this window is authored with the same origin the keyboard editor's is,
            // so an Icon's own lift cancels it out and each one is drawn where it is put.
            Texture texture = spec.glyph
                                  ? tray_icon_[spec.glyph]
                                  : action_icon_[spec.index];

            Keyboard::Mapping mapping = Keyboard::Mapping(key_type(spec.action),
                                                          spec.action);

            tray_.push_back({spec.action,
                             mapping,
                             texture,
                             std::make_unique<Icon>(
                                     std::make_unique<BindingIcon>(mapping,
                                                                   GamepadConfig::Slot::LENGTH),
                             texture,
                             -1)});
        }
    }

    void UIGamepadConfig::draw(float inter) const {
        UIElement::draw(inter);

        for (size_t index = 0; index < tray_.size(); ++index) {
            // A function already bound to an input of the controller leaves its place in the grid
            // empty until it is dragged back off.
            if (!tray_is_bound(index)) {
                tray_[index].icon->draw(position_ + tray_position(index));
            }
        }

        for (uint8_t slot = 0; slot < GamepadConfig::Slot::LENGTH; ++slot) {
            if (const Icon *icon = slot_icons_[slot].get()) {
                icon->draw(position_ + slot_position(static_cast<GamepadConfig::Slot>(slot)));
            }
        }

        key_overlay_.draw(DrawArgument(position_ + kPlateOrigin));
    }

    Cursor::State UIGamepadConfig::send_cursor(bool clicked,
                                               Point<int16_t> cursorpos) {
        Cursor::State dstate = UIDragElement::send_cursor(clicked, cursorpos);

        if (dragged_) {
            return dstate;
        }

        if (clicked) {
            int16_t tray_slot = tray_by_position(cursorpos);

            if (tray_slot >= 0 && !tray_is_bound(tray_slot)) {
                grabbed_ = GamepadConfig::Slot::LENGTH;
                Point<int16_t> offset = cursorpos - position_
                                        - tray_position(tray_slot);

                return grab(*tray_[tray_slot].icon, offset);
            }

            GamepadConfig::Slot slot = slot_by_position(cursorpos);

            if (slot < GamepadConfig::Slot::LENGTH) {
                if (Icon *icon = slot_icons_[slot].get()) {
                    grabbed_ = slot;

                    return grab(*icon,
                                cursorpos - position_ - slot_position(slot));
                }
            }
        } else {
            GamepadConfig::Slot slot = slot_by_position(cursorpos);

            if (slot < GamepadConfig::Slot::LENGTH) {
                Keyboard::Mapping mapping = staged_[slot];

                if (mapping.type == KeyType::Id::ITEM) {
                    show_item(mapping.action);
                } else if (mapping.type == KeyType::Id::SKILL) {
                    show_skill(mapping.action);
                }
            } else if (tray_by_position(cursorpos) < 0) {
                clear_tooltip();
            }
        }

        Cursor::State ret = UIElement::send_cursor(clicked, cursorpos);

        // Telling the touch layer that this icon can be picked up is what lets a drag that never
        // paused start as a grab.
        if (!clicked && ret == Cursor::State::IDLE && can_drag(cursorpos)) {
            ret = Cursor::State::CAN_DRAG;
        }

        return ret;
    }

    bool UIGamepadConfig::send_icon(const Icon &icon, Point<int16_t> cursorpos) {
        std::optional<Keyboard::Mapping> binding = icon.binding();

        if (binding) {
            GamepadConfig::Slot slot = slot_by_position(cursorpos);

            if (slot < GamepadConfig::Slot::LENGTH && slot != grabbed_) {
                unstage(grabbed_);
                stage(slot, *binding);
            }
        }

        // Dropping back onto the tray is how a binding is taken off a controller input without
        // replacing it with another one.
        if (tray_by_position(cursorpos) >= 0) {
            unstage(grabbed_);
        }

        grabbed_ = GamepadConfig::Slot::LENGTH;

        return true;
    }

    void UIGamepadConfig::send_key(int32_t keycode, bool pressed, bool escape) {
        if (pressed && escape) {
            safe_close();
        }
    }

    UIElement::Type UIGamepadConfig::get_type() const {
        return TYPE;
    }

    Button::State UIGamepadConfig::button_pressed(uint16_t buttonid) {
        switch (buttonid) {
            case Buttons::CLOSE:
                safe_close();
                break;
            case Buttons::CANCEL:
                close();
                break;
            case Buttons::DEFAULT: {
                const std::string message =
                        "Would you like to revert to default settings?";

                auto onok = [&](bool ok) {
                    if (!ok) {
                        return;
                    }

                    for (uint8_t slot = 0; slot < GamepadConfig::Slot::LENGTH;
                         ++slot) {
                        stage(static_cast<GamepadConfig::Slot>(slot),
                              GamepadConfig::default_mapping(
                                      static_cast<GamepadConfig::Slot>(slot)));
                    }
                };

                UI::get().emplace<UIOk>(message, onok);
                break;
            }
            case Buttons::DELETE: {
                const std::string message =
                        "Would you like to clear all key bindings?";

                auto onok = [&](bool ok) {
                    if (!ok) {
                        return;
                    }

                    for (uint8_t slot = 0; slot < GamepadConfig::Slot::LENGTH;
                         ++slot) {
                        stage(static_cast<GamepadConfig::Slot>(slot), {});
                    }
                };

                UI::get().emplace<UIOk>(message, onok);
                break;
            }
            case Buttons::OK:
                GamepadConfig::get().commit(staged_);
                close();
                break;
            default:
                break;
        }

        return Button::State::NORMAL;
    }

    void UIGamepadConfig::close() {
        clear_tooltip();
        deactivate();
        reset();
    }

    void UIGamepadConfig::safe_close() {
        if (!dirty_) {
            close();
            return;
        }

        const std::string message = "Do you want to save your changes?";

        auto onok = [&](bool ok) {
            if (ok) {
                GamepadConfig::get().commit(staged_);
            }

            close();
        };

        UI::get().emplace<UIOk>(message, onok);
    }

    void UIGamepadConfig::reset() {
        for (uint8_t slot = 0; slot < GamepadConfig::Slot::LENGTH; ++slot) {
            auto input = static_cast<GamepadConfig::Slot>(slot);

            staged_[input] = GamepadConfig::get().mapping(input);
            slot_icons_[input] = make_icon(staged_[input], input);
        }

        dirty_ = false;
    }

    void UIGamepadConfig::stage(GamepadConfig::Slot slot,
                                Keyboard::Mapping mapping) {
        if (slot >= GamepadConfig::Slot::LENGTH
            || staged_[slot] == mapping) {
            return;
        }

        staged_[slot] = mapping;
        slot_icons_[slot] = make_icon(mapping, slot);
        dirty_ = true;
    }

    void UIGamepadConfig::unstage(GamepadConfig::Slot slot) {
        if (slot >= GamepadConfig::Slot::LENGTH) {
            return;
        }

        stage(slot, {});
    }

    void UIGamepadConfig::update_item_count(InventoryType::Id type,
                                            int16_t slot,
                                            int16_t change) {
        int32_t item_id = inventory_.get_item_id(type, slot);

        for (uint8_t index = 0; index < GamepadConfig::Slot::LENGTH; ++index) {
            Keyboard::Mapping &mapping = staged_[index];

            if (mapping.type != KeyType::Id::ITEM || mapping.action != item_id) {
                continue;
            }

            slot_icons_[index]->set_count(slot_icons_[index]->get_count()
                                          + change);
        }
    }

/// Helpers
    Cursor::State UIGamepadConfig::grab(Icon &icon, Point<int16_t> offset) {
        icon.start_drag(offset);
        UI::get().drag_icon(&icon);

        return Cursor::State::GRABBING;
    }

    std::unique_ptr<Icon> UIGamepadConfig::make_icon(Keyboard::Mapping mapping,
                                                     GamepadConfig::Slot slot) const {
        auto type = mapping.type;

        if (type == KeyType::Id::NONE) {
            return nullptr;
        }

        auto binding = std::make_unique<BindingIcon>(mapping, slot);

        if (type == KeyType::Id::ITEM) {
            int16_t count = inventory_.get_total_item_count(mapping.action);

            return std::make_unique<Icon>(std::move(binding),
                                          item_texture(mapping.action),
                                          count);
        }

        if (type == KeyType::Id::SKILL) {
            return std::make_unique<Icon>(std::move(binding),
                                          skill_texture(mapping.action),
                                          -1);
        }

        std::optional<Texture> texture = action_texture(KeyAction::get_action_by_id(
                mapping.action));

        if (!texture) {
            return nullptr;
        }

        return std::make_unique<Icon>(std::move(binding), *texture, -1);
    }

    std::optional<Texture> UIGamepadConfig::action_texture(
            KeyAction::Id action) const {
        for (const TrayEntry &entry: tray_) {
            if (entry.action == action) {
                return entry.texture;
            }
        }

        return std::nullopt;
    }

    Texture UIGamepadConfig::item_texture(int32_t item_id) const {
        return ItemData::get(item_id).get_icon(false);
    }

    Texture UIGamepadConfig::skill_texture(int32_t skill_id) const {
        return SkillData::get(skill_id).get_icon(SkillData::Icon::NORMAL);
    }

    Point<int16_t> UIGamepadConfig::slot_position(GamepadConfig::Slot slot) const {
        return kPlateOrigin + GamepadConfig::position(slot);
    }

    Point<int16_t> UIGamepadConfig::tray_position(size_t index) const {
        return kTrayOrigin
               + Point<int16_t>(kTrayPitch * static_cast<int16_t>(index % kTrayColumns),
                                kTrayPitch * static_cast<int16_t>(index / kTrayColumns));
    }

    GamepadConfig::Slot UIGamepadConfig::slot_by_position(
            Point<int16_t> cursorpos) const {
        for (uint8_t slot = 0; slot < GamepadConfig::Slot::LENGTH; ++slot) {
            auto input = static_cast<GamepadConfig::Slot>(slot);
            Point<int16_t> top = position_ + slot_position(input);
            Rectangle<int16_t> bounds(top,
                                      top + Point<int16_t>(
                                              GamepadConfig::SLOT_SIZE,
                                              GamepadConfig::SLOT_SIZE));

            if (bounds.contains(cursorpos)) {
                return input;
            }
        }

        return GamepadConfig::Slot::LENGTH;
    }

    int16_t UIGamepadConfig::tray_by_position(Point<int16_t> cursorpos) const {
        for (size_t index = 0; index < tray_.size(); ++index) {
            Point<int16_t> top = position_ + tray_position(index);
            Rectangle<int16_t> bounds(top,
                                      top + Point<int16_t>(
                                              GamepadConfig::SLOT_SIZE,
                                              GamepadConfig::SLOT_SIZE));

            if (bounds.contains(cursorpos)) {
                return static_cast<int16_t>(index);
            }
        }

        return -1;
    }

    bool UIGamepadConfig::tray_is_bound(size_t index) const {
        const Keyboard::Mapping &mapping = tray_[index].mapping;

        for (uint8_t slot = 0; slot < GamepadConfig::Slot::LENGTH; ++slot) {
            if (staged_[slot] == mapping) {
                return true;
            }
        }

        return false;
    }

    bool UIGamepadConfig::can_drag(Point<int16_t> cursorpos) const {
        int16_t tray_slot = tray_by_position(cursorpos);

        if (tray_slot >= 0) {
            return !tray_is_bound(tray_slot);
        }

        GamepadConfig::Slot slot = slot_by_position(cursorpos);

        return slot < GamepadConfig::Slot::LENGTH && slot_icons_[slot];
    }

/// Tooltip
    void UIGamepadConfig::show_item(int32_t item_id) {
        UI::get().show_item(Tooltip::Parent::GAMEPAD_CONFIG, item_id);
    }

    void UIGamepadConfig::show_skill(int32_t skill_id) {
        UI::get().show_skill(Tooltip::Parent::GAMEPAD_CONFIG,
                             skill_id,
                             skillbook_.get_level(skill_id),
                             skillbook_.get_masterlevel(skill_id),
                             skillbook_.get_expiration(skill_id));
    }

    void UIGamepadConfig::clear_tooltip() {
        UI::get().clear_tooltip(Tooltip::Parent::GAMEPAD_CONFIG);
    }

/// BindingIcon
    UIGamepadConfig::BindingIcon::BindingIcon(Keyboard::Mapping mapping,
                                              GamepadConfig::Slot slot) :
            mapping_(mapping),
            slot_(slot) {}

    void UIGamepadConfig::BindingIcon::drop_on_stage() const {
        if (slot_ == GamepadConfig::Slot::LENGTH) {
            return;
        }

        auto window = UI::get().get_element<UIGamepadConfig>();

        if (window) {
            window->get().unstage(slot_);
        }
    }
}  // namespace ms
