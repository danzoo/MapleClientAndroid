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
#include "EquipData.h"

#include <array>
#include <imgwz/wz.hpp>
#include <string>

namespace ms {
EquipData::EquipData(int32_t id) : item_data_(ItemData::get(id)) {
    std::string strid = "0" + std::to_string(id);
    std::string category = item_data_.get_category();
    img::node src = img::wz::character[category][strid + ".img"]["info"];

    cash_ = src["cash"].get_bool();
    trade_block_ = src["tradeBlock"].get_bool();
    slots_ = src["tuc"];

    req_stats_[MapleStat::Id::LEVEL] = src["reqLevel"];
    req_stats_[MapleStat::Id::JOB] = src["reqJob"];
    req_stats_[MapleStat::Id::STR] = src["reqSTR"];
    req_stats_[MapleStat::Id::DEX] = src["reqDEX"];
    req_stats_[MapleStat::Id::INT] = src["reqINT"];
    req_stats_[MapleStat::Id::LUK] = src["reqLUK"];
    def_stats_[EquipStat::Id::STR] = src["incSTR"];
    def_stats_[EquipStat::Id::DEX] = src["incDEX"];
    def_stats_[EquipStat::Id::INT] = src["incINT"];
    def_stats_[EquipStat::Id::LUK] = src["incLUK"];
    def_stats_[EquipStat::Id::WATK] = src["incPAD"];
    def_stats_[EquipStat::Id::WDEF] = src["incPDD"];
    def_stats_[EquipStat::Id::MAGIC] = src["incMAD"];
    def_stats_[EquipStat::Id::MDEF] = src["incMDD"];
    def_stats_[EquipStat::Id::HP] = src["incMHP"];
    def_stats_[EquipStat::Id::MP] = src["incMMP"];
    def_stats_[EquipStat::Id::ACC] = src["incACC"];
    def_stats_[EquipStat::Id::AVOID] = src["incEVA"];
    def_stats_[EquipStat::Id::HANDS] = src["incHANDS"];
    def_stats_[EquipStat::Id::SPEED] = src["incSPEED"];
    def_stats_[EquipStat::Id::JUMP] = src["incJUMP"];

    constexpr size_t NON_WEAPON_TYPES = 15;
    constexpr size_t WEAPON_OFFSET = NON_WEAPON_TYPES + 15;
    constexpr size_t WEAPON_TYPES = 20;
    size_t index = (id / 10000) - 100;

    if (index < NON_WEAPON_TYPES) {
        const std::array<std::string, NON_WEAPON_TYPES> types = {
            "帽子", "脸部",   "眼部",  "耳环", "上衣",
            "套装", "下衣",   "鞋子",  "手套", "盾牌",
            "披风", "戒指",   "项链",  "腰带", "徽章"
        };

        constexpr EquipSlot::Id equipslots[NON_WEAPON_TYPES] = {
            EquipSlot::Id::HAT,      EquipSlot::Id::FACE,
            EquipSlot::Id::EYE_ACC,  EquipSlot::Id::EARACC,
            EquipSlot::Id::TOP,      EquipSlot::Id::TOP,
            EquipSlot::Id::BOTTOM,   EquipSlot::Id::SHOES,
            EquipSlot::Id::GLOVES,   EquipSlot::Id::SHIELD,
            EquipSlot::Id::CAPE,     EquipSlot::Id::RING1,
            EquipSlot::Id::PENDANT1, EquipSlot::Id::BELT,
            EquipSlot::Id::MEDAL
        };

        type_ = types[index];
        eq_slot_ = equipslots[index];
    } else if (index >= WEAPON_OFFSET && index < WEAPON_OFFSET + WEAPON_TYPES) {
        const std::array<std::string, WEAPON_TYPES> types = {
            "单手剑",
            "单手斧",
            "单手锤",
            "短剑",
            "",
            "",
            "",
            "魔杖",
            "权杖",
            "",
            "双手剑",
            "双手斧",
            "双手锤",
            "长矛",
            "棍棒",
            "弓",
            "弩",
            "爪",
            "拳套",
            "火枪"
        };

        size_t weaponindex = index - WEAPON_OFFSET;
        type_ = types[weaponindex];
        eq_slot_ = EquipSlot::Id::WEAPON;
    } else {
        type_ = "商城道具";
        eq_slot_ = EquipSlot::Id::NONE;
    }
}

bool EquipData::is_valid() const {
    return item_data_.is_valid();
}

EquipData::operator bool() const {
    return is_valid();
}

bool EquipData::is_weapon() const {
    return eq_slot_ == EquipSlot::Id::WEAPON;
}

int16_t EquipData::get_reqstat(MapleStat::Id stat) const {
    return req_stats_[stat];
}

int16_t EquipData::get_defstat(EquipStat::Id stat) const {
    return def_stats_[stat];
}

EquipSlot::Id EquipData::get_eqslot() const {
    return eq_slot_;
}

uint8_t EquipData::get_slots() const {
    return slots_;
}

const std::string &EquipData::get_type() const {
    return type_;
}

const ItemData &EquipData::get_itemdata() const {
    return item_data_;
}
}  // namespace ms