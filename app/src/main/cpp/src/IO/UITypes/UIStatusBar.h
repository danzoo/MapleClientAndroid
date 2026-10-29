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

#include "../../Character/CharStats.h"
#include "../../Graphics/SpecialText.h"
#include "../Components/Gauge.h"
#include "../Components/Icon.h"
#include "../Components/Charset.h"
#include "../QuickSlots.h"
#include "../UIElement.h"

namespace ms {
class UIStatusBar : public UIElement {
public:
    static constexpr Type TYPE = UIElement::Type::STATUS_BAR;
    static constexpr bool FOCUSED = false;
    static constexpr bool TOGGLED = true;

    enum MenuType { MENU, SETTING, COMMUNITY, CHARACTER, EVENT };

    UIStatusBar(const CharStats &stats, uint8_t channel_count);

    void draw(float alpha) const override;

    void update() override;

    bool is_in_range(Point<int16_t> cursorpos) const override;
    bool send_icon(const Icon &icon, Point<int16_t> cursorpos) override;
    Cursor::State send_cursor(bool clicked, Point<int16_t> cursorpos) override;
    void send_key(int32_t keycode, bool pressed, bool escape) override;

    UIElement::Type get_type() const override;

    void toggle_qs();

    void toggle_menu();

    void remove_menus();

    bool is_menu_active();

    // Whether a point falls on one of the strip's cells, and whether that cell is showing an icon.
    // The hit routing asks these before it asks which window is on top: the strip is part of the HUD
    // and sits at the bottom of the screen, where any window can be dragged across it, so a window
    // laid over that corner must not swallow a drop meant for a cell.
    bool over_quickslot(Point<int16_t> cursorpos) const;
    bool over_filled_quickslot(Point<int16_t> cursorpos) const;

protected:
    Button::State button_pressed(uint16_t buttonid) override;

private:
    static constexpr int16_t QUICKSLOT_MAX_ = 211;

    float getexppercent() const;

    float gethppercent() const;

    float getmppercent() const;

    void toggle_qs(bool quick_slot_active);

    void toggle_setting();

    void toggle_community();

    void toggle_character();

    void toggle_event();

    void remove_active_menu(MenuType type);

    Point<int16_t> get_quickslot_pos() const;

    // Where the strip's art is drawn, which is the one place both the drawing and the hit test read.
    Point<int16_t> quickslot_art_pos() const;
    // The top-left of a slot's cell, in the same space the screen is drawn in.
    Point<int16_t> quickslot_cell(int16_t slot) const;
    // The slot under the cursor, or -1 when the cursor is not on one.
    int16_t quickslot_by_position(Point<int16_t> cursorpos) const;
    // Rebuilding only the icons whose binding has moved since they were last drawn.
    void refresh_quickslots();
    // Whether a window that takes this kind of binding is on screen, which is what decides whether a
    // tap on a slot picks its icon up or fires what the slot is bound to.
    static bool takes_binding(Keyboard::Mapping mapping);
    Cursor::State grab_quickslot(int16_t slot, Point<int16_t> cursorpos);

    enum Buttons : uint16_t {
        BT_CASHSHOP,
        BT_MENU,
        BT_OPTIONS,
        BT_CHARACTER,
        BT_COMMUNITY,
        BT_EVENT,
        BT_FOLD_QS,
        BT_EXTEND_QS,
        BT_MENU_QUEST,
        BT_MENU_MEDAL,
        BT_MENU_UNION,
        BT_MENU_MONSTER_COLLECTION,
        BT_MENU_AUCTION,
        BT_MENU_MONSTER_LIFE,
        BT_MENU_BATTLE,
        BT_MENU_ACHIEVEMENT,
        BT_MENU_FISHING,
        BT_MENU_HELP,
        BT_MENU_CLAIM,
        BT_SETTING_CHANNEL,
        BT_SETTING_OPTION,
        BT_SETTING_KEYS,
        BT_SETTING_JOYPAD,
        BT_SETTING_QUIT,
        BT_COMMUNITY_FRIENDS,
        BT_COMMUNITY_PARTY,
        BT_COMMUNITY_GUILD,
        BT_COMMUNITY_MAPLECHAT,
        BT_CHARACTER_INFO,
        BT_CHARACTER_STAT,
        BT_CHARACTER_SKILL,
        BT_CHARACTER_EQUIP,
        BT_CHARACTER_ITEM,
        BT_EVENT_SCHEDULE,
        BT_EVENT_DAILY
    };

    const CharStats &stats_;

    Gauge exp_bar_;
    Gauge hp_bar_;
    Gauge mp_bar_;
    Charset stat_set_;
    Charset hpmp_set_;
    Charset level_set_;
    std::array<Texture, 2> quickslot_;
    // One icon per slot, held rather than made on the fly: picking one up hands a reference to the
    // drag layer, which keeps reading it for as long as the drag lasts.
    std::array<std::unique_ptr<Icon>, QuickSlots::LENGTH> quickslot_icons_;
    // The binding each of those icons was made from, so a slot is only rebuilt when it changes.
    std::array<Keyboard::Mapping, QuickSlots::LENGTH> quickslot_drawn_;
    std::array<Texture, 5> menu_title_;
    std::array<Texture, 3> menu_background_;
    OutlinedText name_label_;
    std::vector<Sprite> hpmp_sprites_;

    Point<int16_t> exp_pos_;
    Point<int16_t> hpmp_pos_;
    Point<int16_t> hpset_pos_;
    Point<int16_t> mpset_pos_;
    Point<int16_t> statset_pos_;
    Point<int16_t> levelset_pos_;
    Point<int16_t> namelabel_pos_;
    Point<int16_t> quickslot_pos_;
    Point<int16_t> quickslot_adj_;
    Point<int16_t> quickslot_qs_adj_;
    Point<int16_t> menu_pos_;
    Point<int16_t> setting_pos_;
    Point<int16_t> community_pos_;
    Point<int16_t> character_pos_;
    Point<int16_t> event_pos_;
    int16_t quickslot_min_;
    int16_t position_x_;
    int16_t position_y_;

    bool quickslot_active_;
    int16_t VWIDTH_;
    // The screen the interface is laid out on, which is the one this bar is drawn against. The
    // branches above still read the view's own width, because that is what selects the artwork.
    int16_t UIWIDTH_;
    int16_t UIHEIGHT_;

    uint8_t channel_count_;

    bool menu_active_;
    bool setting_active_;
    bool community_active_;
    bool character_active_;
    bool event_active_;
};
}  // namespace ms