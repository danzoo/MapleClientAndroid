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

#include "../Character/Inventory/Equip.h"
#include "../Character/MapleStat.h"
#include "../Graphics/Text.h"
#include "../Template/BoolPair.h"
#include "Charset.h"
#include "Tooltip.h"

#include <optional>

namespace ms {
class EquipTooltip : public Tooltip {
public:
    EquipTooltip();

    void set_equip(Tooltip::Parent parent, int16_t invpos);

    // An item for sale has never been held, so there is no slot of the character's to read it from.
    // The panel is built from the goods' own id instead, and the piece it would replace still comes
    // from the inventory.
    void set_goods(Tooltip::Parent parent, int32_t item_id);

    bool build(NativeTooltip::List &list, Point<int16_t> position) const override;

private:
    void set_panel(Tooltip::Parent parent);

    // The panel showing the piece the character is already wearing, put beside the panel showing the
    // piece under the finger so the two can be read against each other. It is written out first, as
    // it was drawn first.
    void build_preview(NativeTooltip::List &list, Point<int16_t> position) const;
    int16_t inv_pos_;
    int32_t goods_id_;
    std::optional<Equip> goods_;
    int16_t inv_pos_preview_;
    int16_t height_;
    int16_t height_preview_;
    bool has_desc_;
    bool has_desc_preview_;
    bool has_slots_;
    bool has_slots_preview_;
    bool is_weapon_;
    bool is_weapon_preview_;
    EnumMap<MapleStat::Id, std::string> req_stat_strings_;
    EnumMap<MapleStat::Id, std::string> req_stat_strings_preview_;
    Texture item_icon_;
    Texture item_icon_preview_;

    Text name_;
    Text name_preview_;
    Text desc_;
    Text desc_preview_;
    Text potflag_;
    Text potflag_preview_;
    Text category_;
    Text category_preview_;
    Text wepspeed_;
    Text wepspeed_preview_;
    Text slots_;
    Text slots_preview_;
    Text hammers_;
    Text hammers_preview_;
    Text atkinc_;
    Text atkinc_preview_;
    EnumMap<EquipStat::Id, Text> stat_labels_;
    EnumMap<EquipStat::Id, Text> stat_labels_preview_;

    Texture top_;
    Texture mid_;
    Texture line_;
    Texture bot_;
    Texture base_;

    EnumMap<Equip::Potential, Texture> potential_;
    // The file each of the shared canvases was exported to, kept beside the canvas it names. A
    // preview handed to the platform has to be told which picture goes in which box, and the archive
    // it was read out of is on this side of the bridge only.
    EnumMap<Equip::Potential, std::string> potential_names_;
    Equip::Potential prank_;
    Equip::Potential prank_preview_;

    Texture cover_;
    Texture item_cover_;
    BoolPair<Texture> type_;

    std::vector<MapleStat::Id> requirements_;
    EnumMap<MapleStat::Id, BoolPair<Texture>> req_stat_textures_;
    EnumMap<MapleStat::Id, BoolPair<std::string>> req_stat_names_;
    EnumMap<MapleStat::Id, bool> can_equip_;
    EnumMap<MapleStat::Id, bool> can_equip_preview_;
    EnumMap<MapleStat::Id, Point<int16_t>> req_stat_positions_;
    BoolPair<Charset> reqset_;
    BoolPair<Charset> lvset_;
    BoolPair<Charset> atkincset_;

    Texture jobs_back_;
    BoolPair<std::map<uint8_t, Texture>> jobs_;
    std::vector<uint8_t> okjobs_;
    std::vector<uint8_t> okjobs_preview_;
};
}  // namespace ms