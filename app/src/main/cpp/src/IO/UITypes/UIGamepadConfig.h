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
#include <memory>
#include <optional>
#include <vector>

#include "../../Character/Inventory/Inventory.h"
#include "../../Character/SkillBook.h"
#include "../Components/Icon.h"
#include "../GamepadConfig.h"
#include "../UIDragElement.h"

namespace ms {
    // The controller binding window: the picture of a pad with one drop target per input, and a
    // tray of the functions that can be dragged onto them. Works like the keyboard editor - grab
    // an icon, drop it on a key, press OK - except that a controller input keeps its own binding
    // instead of standing in for a key, so the same function may sit on several of them.
    class UIGamepadConfig : public UIDragElement<PosGAMEPADCONFIG> {
    public:
        static constexpr Type TYPE = UIElement::Type::GAMEPAD_CONFIG;
        static constexpr bool FOCUSED = false;
        static constexpr bool TOGGLED = true;

        UIGamepadConfig(const Inventory &inventory, const SkillBook &skillbook);

        void draw(float inter) const override;

        Cursor::State send_cursor(bool clicked, Point<int16_t> cursorpos) override;

        bool send_icon(const Icon &icon, Point<int16_t> cursorpos) override;

        void send_key(int32_t keycode, bool pressed, bool escape) override;

        UIElement::Type get_type() const override;

        void close();

        void update_item_count(InventoryType::Id type,
                               int16_t slot,
                               int16_t change);

        void unstage(GamepadConfig::Slot slot);

    protected:
        Button::State button_pressed(uint16_t buttonid) override;

    private:
        enum Buttons : uint16_t { CLOSE, CANCEL, DEFAULT, DELETE, OK };

        // One of the icons on show: either a function still waiting in the tray, or the binding
        // sitting in a controller input. It knows which input it came from so that dropping it
        // outside the window empties that one.
        class BindingIcon : public Icon::Type {
        public:
            BindingIcon(Keyboard::Mapping mapping, GamepadConfig::Slot slot);

            void drop_on_stage() const override;

            void drop_on_equips(EquipSlot::Id) const override {}

            bool drop_on_items(InventoryType::Id,
                               EquipSlot::Id,
                               int16_t,
                               bool) const override {
                return true;
            }

            void drop_on_bindings(Point<int16_t>, bool) const override {}

            std::optional<Keyboard::Mapping> binding() const override {
                return mapping_;
            }

            void set_count(int16_t) override {}

            Icon::IconType get_type() override { return Icon::IconType::GAMEPAD; }

        private:
            Keyboard::Mapping mapping_;
            GamepadConfig::Slot slot_;
        };

        struct TrayEntry {
            KeyAction::Id action;
            Keyboard::Mapping mapping;
            // Kept alongside the icon because an icon hides its own texture behind a thirty two
            // pixel adjustment, and a slot icon has to be built from the same one the tray shows.
            Texture texture;
            std::unique_ptr<Icon> icon;
        };

        void load_tray();

        Point<int16_t> slot_position(GamepadConfig::Slot slot) const;
        Point<int16_t> tray_position(size_t index) const;

        GamepadConfig::Slot slot_by_position(Point<int16_t> cursorpos) const;
        int16_t tray_by_position(Point<int16_t> cursorpos) const;

        // Whether a tray icon's function is already sitting on some input of the controller, which
        // is what takes it out of the tray until it is dragged back off.
        bool tray_is_bound(size_t index) const;

        // Whether the cursor is over something it could pick up without pressing first, which is
        // how a quick drag rather than a held press grabs an icon.
        bool can_drag(Point<int16_t> cursorpos) const;

        void stage(GamepadConfig::Slot slot, Keyboard::Mapping mapping);

        Cursor::State grab(Icon &icon, Point<int16_t> offset);

        std::unique_ptr<Icon> make_icon(Keyboard::Mapping mapping,
                                        GamepadConfig::Slot slot) const;

        std::optional<Texture> action_texture(KeyAction::Id action) const;
        Texture item_texture(int32_t item_id) const;
        Texture skill_texture(int32_t skill_id) const;

        void safe_close();
        void reset();

        void show_item(int32_t item_id);
        void show_skill(int32_t skill_id);
        void clear_tooltip();

        const Inventory &inventory_;
        const SkillBook &skillbook_;

        bool dirty_;

        // Which input the icon currently in the cursor was picked up from, so that putting it
        // down somewhere else empties the one it came from. LENGTH while the cursor holds a tray
        // icon or one dragged in from the skill book or the inventory.
        GamepadConfig::Slot grabbed_;

        img::node action_icon_;
        img::node tray_icon_;

        // The plate's own key labels, which have to stay readable through whatever is bound to the
        // input they name, so they are the last thing drawn instead of part of the background.
        Texture key_overlay_;

        std::array<Keyboard::Mapping, GamepadConfig::Slot::LENGTH> staged_;
        std::array<std::unique_ptr<Icon>, GamepadConfig::Slot::LENGTH> slot_icons_;
        std::vector<TrayEntry> tray_;
    };
}  // namespace ms
