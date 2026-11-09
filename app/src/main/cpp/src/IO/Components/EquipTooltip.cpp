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
#include "EquipTooltip.h"

#include <imgwz/wz.hpp>

#include "../../Data/WeaponData.h"
#include "../../Gameplay/Stage.h"

namespace ms {
namespace {
// The columns text is laid out in, measured from the left edge of the panel: the name across the top
// of it, and everything else under the divider. The attack summary is a column of digits, and the
// label over it is right-aligned to the edge those digits end at.
constexpr int16_t NAME_X = 17;
constexpr int16_t STAT_X = 13;
constexpr int16_t DESC_X = 9;
constexpr int16_t SUMMARY_X = 239;
constexpr int16_t SUMMARY_W = 11;

// Two labels are not laid out from the left of their column: the potential line is centred on the
// panel, and the attack label ends where its digits end. Their origin is the anchor, not the column,
// so reading either from STAT_X makes the word grow leftwards out of the frame.
constexpr int16_t POTFLAG_X = 130;
constexpr int16_t ATKINC_X = SUMMARY_X + SUMMARY_W - 2;
}  // namespace

// TODO: Add blue dot next to name
EquipTooltip::EquipTooltip() {
    img::node Item = img::wz::ui["UIToolTip.img"]["Item"];
    img::node Frame = Item["Frame"];
    img::node ItemIcon = Item["ItemIcon"];
    img::node Equip = Item["Equip"];
    img::node EquipCan = Equip["Can"];
    img::node EquipCannot = Equip["Cannot"];

    top_ = Frame["top"];
    mid_ = Frame["line"];
    line_ = Frame["dotline"];
    bot_ = Frame["bottom"];
    base_ = ItemIcon["base"];
    cover_ = Frame["cover"];
    item_cover_ = ItemIcon["cover"];
    type_[true] = ItemIcon["new"];
    type_[false] = ItemIcon["old"];

    potential_[Equip::Potential::POT_NONE] = Texture();
    potential_[Equip::Potential::POT_HIDDEN] = ItemIcon["0"];
    potential_[Equip::Potential::POT_RARE] = ItemIcon["1"];
    potential_[Equip::Potential::POT_EPIC] = ItemIcon["2"];
    potential_[Equip::Potential::POT_UNIQUE] = ItemIcon["3"];
    potential_[Equip::Potential::POT_LEGENDARY] = ItemIcon["4"];

    potential_names_[Equip::Potential::POT_NONE] = "";
    potential_names_[Equip::Potential::POT_HIDDEN] = "ItemIcon/0";
    potential_names_[Equip::Potential::POT_RARE] = "ItemIcon/1";
    potential_names_[Equip::Potential::POT_EPIC] = "ItemIcon/2";
    potential_names_[Equip::Potential::POT_UNIQUE] = "ItemIcon/3";
    potential_names_[Equip::Potential::POT_LEGENDARY] = "ItemIcon/4";

    requirements_.push_back(MapleStat::Id::LEVEL);
    requirements_.push_back(MapleStat::Id::STR);
    requirements_.push_back(MapleStat::Id::DEX);
    requirements_.push_back(MapleStat::Id::INT);
    requirements_.push_back(MapleStat::Id::LUK);

    req_stat_textures_[MapleStat::Id::LEVEL][false] = EquipCannot["reqLEV"];
    req_stat_textures_[MapleStat::Id::LEVEL][true] = EquipCan["reqLEV"];
    req_stat_textures_[MapleStat::Id::FAME][false] = EquipCannot["reqPOP"];
    req_stat_textures_[MapleStat::Id::FAME][true] = EquipCan["reqPOP"];
    req_stat_textures_[MapleStat::Id::STR][false] = EquipCannot["reqSTR"];
    req_stat_textures_[MapleStat::Id::STR][true] = EquipCan["reqSTR"];
    req_stat_textures_[MapleStat::Id::DEX][false] = EquipCannot["reqDEX"];
    req_stat_textures_[MapleStat::Id::DEX][true] = EquipCan["reqDEX"];
    req_stat_textures_[MapleStat::Id::INT][false] = EquipCannot["reqINT"];
    req_stat_textures_[MapleStat::Id::INT][true] = EquipCan["reqINT"];
    req_stat_textures_[MapleStat::Id::LUK][false] = EquipCannot["reqLUK"];
    req_stat_textures_[MapleStat::Id::LUK][true] = EquipCan["reqLUK"];

    req_stat_names_[MapleStat::Id::LEVEL][false] = "Equip/Cannot/reqLEV";
    req_stat_names_[MapleStat::Id::LEVEL][true] = "Equip/Can/reqLEV";
    req_stat_names_[MapleStat::Id::FAME][false] = "Equip/Cannot/reqPOP";
    req_stat_names_[MapleStat::Id::FAME][true] = "Equip/Can/reqPOP";
    req_stat_names_[MapleStat::Id::STR][false] = "Equip/Cannot/reqSTR";
    req_stat_names_[MapleStat::Id::STR][true] = "Equip/Can/reqSTR";
    req_stat_names_[MapleStat::Id::DEX][false] = "Equip/Cannot/reqDEX";
    req_stat_names_[MapleStat::Id::DEX][true] = "Equip/Can/reqDEX";
    req_stat_names_[MapleStat::Id::INT][false] = "Equip/Cannot/reqINT";
    req_stat_names_[MapleStat::Id::INT][true] = "Equip/Can/reqINT";
    req_stat_names_[MapleStat::Id::LUK][false] = "Equip/Cannot/reqLUK";
    req_stat_names_[MapleStat::Id::LUK][true] = "Equip/Can/reqLUK";

    req_stat_positions_[MapleStat::Id::LEVEL] = Point<int16_t>(97, 47);
    req_stat_positions_[MapleStat::Id::STR] = Point<int16_t>(97, 62);
    req_stat_positions_[MapleStat::Id::LUK] = Point<int16_t>(177, 62);
    req_stat_positions_[MapleStat::Id::DEX] = Point<int16_t>(97, 71);
    req_stat_positions_[MapleStat::Id::INT] = Point<int16_t>(177, 71);

    reqset_[false] = Charset(EquipCannot, Charset::Alignment::LEFT, "Equip/Cannot");
    reqset_[true] = Charset(EquipCan, Charset::Alignment::LEFT, "Equip/Can");

    lvset_[false] = Charset(EquipCannot, Charset::Alignment::LEFT, "Equip/Cannot");
    lvset_[true] = Charset(Equip["YellowNumber"], Charset::Alignment::LEFT,
                           "Equip/YellowNumber");

    atkincset_[false] =
        Charset(Equip["Summary"]["decline"], Charset::Alignment::RIGHT,
                "Equip/Summary/decline");
    atkincset_[true] =
        Charset(Equip["Summary"]["incline"], Charset::Alignment::RIGHT,
                "Equip/Summary/incline");

    jobs_back_ = Equip["Job"]["normal"];
    jobs_[false][0] = Equip["Job"]["disable"]["0"];
    jobs_[false][1] = Equip["Job"]["disable"]["1"];
    jobs_[false][2] = Equip["Job"]["disable"]["2"];
    jobs_[false][3] = Equip["Job"]["disable"]["3"];
    jobs_[false][4] = Equip["Job"]["disable"]["4"];
    jobs_[false][5] = Equip["Job"]["disable"]["5"];
    jobs_[true][0] = Equip["Job"]["enable"]["0"];
    jobs_[true][1] = Equip["Job"]["enable"]["1"];
    jobs_[true][2] = Equip["Job"]["enable"]["2"];
    jobs_[true][3] = Equip["Job"]["enable"]["3"];
    jobs_[true][4] = Equip["Job"]["enable"]["4"];
    jobs_[true][5] = Equip["Job"]["enable"]["5"];

    inv_pos_ = 0;
    inv_pos_preview_ = 0;
    goods_id_ = 0;
}

void EquipTooltip::set_equip(Tooltip::Parent parent, int16_t ivp) {
    // A control belongs to the piece it was put up for, so the first thing any new reading of the
    // panel does is drop the one the last piece had. The window that wants one hands it over again
    // once the preview has been pointed at the piece.
    clear_actions();

    if (goods_id_ == 0 && inv_pos_ == ivp) {
        return;
    }

    goods_id_ = 0;
    goods_.reset();
    inv_pos_ = ivp;
    inv_pos_preview_ = 0;

    set_panel(parent);
}

void EquipTooltip::set_goods(Tooltip::Parent parent, int32_t item_id) {
    clear_actions();

    if (goods_id_ == item_id) {
        return;
    }

    goods_id_ = item_id;
    goods_.reset();
    inv_pos_ = 0;
    inv_pos_preview_ = 0;

    if (item_id != 0) {
        // A good has never been held by anyone: nothing has been scrolled into it, it has never been
        // leveled, and it carries exactly the stats its type is written down with. That is what there
        // is to say about it, and it is the same panel with the same readings as a piece the
        // character owns - including, beside it, the piece he would be taking the new one off of.
        const EquipData &equipdata = EquipData::get(item_id);
        EnumMap<EquipStat::Id, uint16_t> defstats;

        for (EquipStat::Id es : EquipStat::values) {
            defstats[es] = static_cast<uint16_t>(equipdata.get_defstat(es));
        }

        goods_.emplace(item_id,
                       0,
                       "",
                       0,
                       equipdata.get_slots(),
                       0,
                       defstats,
                       0,
                       0,
                       0);
    }

    set_panel(parent);
}

void EquipTooltip::set_panel(Tooltip::Parent parent) {
    // The panel is as wide as the art that caps it, and every wrap is taken from that, held back
    // from its right edge by the same margin the text is placed off its left. A wrap that runs to
    // the edge itself lays the last line of a long name over the frame.
    int16_t panel_w = top_.width();
    int16_t edge = panel_w - Tooltip::TEXT_MARGIN;
    int16_t name_w = edge - NAME_X;
    int16_t desc_w = edge - DESC_X;
    int16_t potflag_w = edge - STAT_X;

    // Right-aligned against the edge the attack summary's digits end at, so the label sits over them
    // whatever it says.
    int16_t atkinc_w = SUMMARY_X + SUMMARY_W - STAT_X;

    const Player &player = Stage::get().get_player();

    InventoryType::Id invtype;

    switch (parent) {
        case Tooltip::Parent::ITEM_INVENTORY:
        case Tooltip::Parent::SHOP: invtype = InventoryType::Id::EQUIP; break;
        case Tooltip::Parent::EQUIP_INVENTORY:
            invtype = InventoryType::Id::EQUIPPED;
            break;
        default: invtype = InventoryType::Id::NONE;
    }

    const Inventory &inventory = player.get_inventory();

    // A good is read the same way as a piece in the character's bag: the panel below only ever asks
    // for "the equip this preview is about", and the copy built from the item's type lines up with
    // that. Nothing is looked up in the inventory when there is a good to show instead.
    std::optional<std::reference_wrapper<const Equip>> oequip =
            goods_id_ != 0
            ? std::optional<std::reference_wrapper<const Equip>>(
                      std::cref(*goods_))
            : inventory.get_equip(invtype, inv_pos_);

    const CharStats &stats = player.get_stats();

    if (invtype == InventoryType::Id::EQUIP) {
        const int32_t item_id = oequip->get().get_item_id();
        const EquipData &equipdata = EquipData::get(item_id);
        EquipSlot::Id eqslot = equipdata.get_eqslot();

        if (inventory.has_equipped(eqslot)) {
            auto eequip =
                inventory.get_equip(InventoryType::Id::EQUIPPED, eqslot);

            if (eequip) {
                const Equip &equip = *eequip;

                int32_t item_id = equip.get_item_id();

                const EquipData &equipdata = EquipData::get(item_id);
                const ItemData &itemdata = equipdata.get_itemdata();

                height_preview_ = 540;

                item_icon_preview_ = itemdata.get_icon(false);

                for (auto &ms : requirements_) {
                    can_equip_preview_[ms] =
                        stats.get_stat(ms) >= equipdata.get_reqstat(ms);
                    std::string reqstr =
                        std::to_string(equipdata.get_reqstat(ms));

                    if (ms != MapleStat::Id::LEVEL) {
                        reqstr.insert(0, 3 - reqstr.size(), '0');
                    }

                    req_stat_strings_preview_[ms] = reqstr;
                }

                okjobs_preview_.clear();

                switch (equipdata.get_reqstat(MapleStat::Id::JOB)) {
                    case 0:
                        okjobs_preview_.push_back(0);
                        okjobs_preview_.push_back(1);
                        okjobs_preview_.push_back(2);
                        okjobs_preview_.push_back(3);
                        okjobs_preview_.push_back(4);
                        okjobs_preview_.push_back(5);
                        can_equip_preview_[MapleStat::Id::JOB] = true;
                        break;
                    case 1:
                        okjobs_preview_.push_back(1);
                        can_equip_preview_[MapleStat::Id::JOB] =
                            (stats.get_stat(MapleStat::Id::JOB) / 100 == 1)
                            || (stats.get_stat(MapleStat::Id::JOB) / 100 >= 20);
                        break;
                    case 2:
                        okjobs_preview_.push_back(2);
                        can_equip_preview_[MapleStat::Id::JOB] =
                            stats.get_stat(MapleStat::Id::JOB) / 100 == 2;
                        break;
                    case 4:
                        okjobs_preview_.push_back(3);
                        can_equip_preview_[MapleStat::Id::JOB] =
                            stats.get_stat(MapleStat::Id::JOB) / 100 == 3;
                        break;
                    case 8:
                        okjobs_preview_.push_back(4);
                        can_equip_preview_[MapleStat::Id::JOB] =
                            stats.get_stat(MapleStat::Id::JOB) / 100 == 4;
                        break;
                    case 16:
                        okjobs_preview_.push_back(5);
                        can_equip_preview_[MapleStat::Id::JOB] =
                            stats.get_stat(MapleStat::Id::JOB) / 100 == 5;
                        break;
                    default:
                        can_equip_preview_[MapleStat::Id::JOB] = false;
                        break;
                }

                prank_preview_ = equip.get_potrank();

                Color::Name potcolor_preview = Color::Name::WHITE;
                std::string potstr_preview;

                switch (prank_preview_) {
                    case Equip::Potential::POT_HIDDEN:
                        potcolor_preview = Color::Name::RED;
                        potstr_preview = "（未知潜能）";
                        break;
                    case Equip::Potential::POT_RARE:
                        potstr_preview = "（稀有潜能）";
                        break;
                    case Equip::Potential::POT_EPIC:
                        potstr_preview = "（史诗潜能）";
                        break;
                    case Equip::Potential::POT_UNIQUE:
                        potstr_preview = "（独特潜能）";
                        break;
                    case Equip::Potential::POT_LEGENDARY:
                        potstr_preview = "（传说潜能）";
                        break;
                    default: height_preview_ -= 16; break;
                }

                if (prank_preview_ != Equip::Potential::POT_NONE) {
                    potflag_preview_ = Text(Text::Font::A11M,
                                            Text::Alignment::CENTER,
                                            potcolor_preview,
                                            potstr_preview,
                                            potflag_w);
                }

                Color::Name namecolor;

                switch (equip.get_quality()) {
                    case EquipQuality::Id::GREY:
                        namecolor = Color::Name::LIGHTGREY;
                        break;
                    case EquipQuality::Id::ORANGE:
                        namecolor = Color::Name::ORANGE;
                        break;
                    case EquipQuality::Id::BLUE:
                        namecolor = Color::Name::MEDIUMBLUE;
                        break;
                    case EquipQuality::Id::VIOLET:
                        namecolor = Color::Name::VIOLET;
                        break;
                    case EquipQuality::Id::GOLD:
                        namecolor = Color::Name::YELLOW;
                        break;
                    default: namecolor = Color::Name::WHITE; break;
                }

                std::string namestr = itemdata.get_name();
                const int8_t reqGender = itemdata.get_gender();
                const bool female = stats.get_female();

                switch (reqGender) {
                    case 0:  // Male
                        namestr += "（男）";
                        break;
                    case 1:  // Female
                        namestr += "（女）";
                        break;
                    case 2:  // Unisex
                    default: break;
                }

                if (equip.get_level() > 0) {
                    namestr.append(" (+");
                    namestr.append(std::to_string(equip.get_level()));
                    namestr.append(")");
                }

                name_preview_ = Text(Text::Font::A12B,
                                     Text::Alignment::LEFT,
                                     namecolor,
                                     namestr,
                                     name_w);
                atkinc_preview_ = Text(Text::Font::A11M,
                                       Text::Alignment::RIGHT,
                                       Color::Name::DUSTYGRAY,
                                       "攻击提升",
                                       atkinc_w);

                std::string desctext = itemdata.get_desc();
                has_desc_preview_ = !desctext.empty();

                if (has_desc_preview_) {
                    desc_preview_ = Text(Text::Font::A12M,
                                         Text::Alignment::LEFT,
                                         Color::Name::WHITE,
                                         desctext,
                                         desc_w);
                    height_preview_ += desc_preview_.height() + 10;
                }

                category_preview_ = Text(Text::Font::A11M,
                                         Text::Alignment::LEFT,
                                         Color::Name::WHITE,
                                         "类型：" + equipdata.get_type());

                is_weapon_preview_ = equipdata.is_weapon();

                if (is_weapon_preview_) {
                    const WeaponData &weapon = WeaponData::get(item_id);
                    wepspeed_preview_ =
                        Text(Text::Font::A11M,
                             Text::Alignment::LEFT,
                             Color::Name::WHITE,
                             "攻击速度：" + weapon.getspeedstring());
                } else {
                    height_preview_ -= 18;
                }

                has_slots_preview_ =
                    (equip.get_slots() > 0) || (equip.get_level() > 0);

                if (has_slots_preview_) {
                    slots_preview_ =
                        Text(Text::Font::A11M,
                             Text::Alignment::LEFT,
                             Color::Name::WHITE,
                             "剩余强化次数："
                                 + std::to_string(equip.get_slots()));

                    std::string vicious = std::to_string(equip.get_vicious());

                    if (equip.get_vicious() > 1) {
                        vicious.append("（上限）");
                    }

                    hammers_preview_ = Text(Text::Font::A11M,
                                            Text::Alignment::LEFT,
                                            Color::Name::WHITE,
                                            "强化锤使用次数：" + vicious);
                } else {
                    height_preview_ -= 36;
                }

                stat_labels_preview_.clear();

                for (EquipStat::Id es = EquipStat::Id::STR;
                     es <= EquipStat::Id::JUMP;
                     es = static_cast<EquipStat::Id>(es + 1)) {
                    if (equip.get_stat(es) > 0) {
                        int16_t delta =
                            equip.get_stat(es) - equipdata.get_defstat(es);
                        std::string statstr =
                            std::to_string(equip.get_stat(es));

                        if (delta != 0) {
                            statstr.append(" (");
                            statstr.append((delta < 0) ? "-" : "+");
                            statstr.append(std::to_string(abs(delta)) + ")");
                        }

                        stat_labels_preview_[es] = Text(
                            Text::Font::A11M,
                            Text::Alignment::LEFT,
                            Color::Name::WHITE,
                            EquipStat::names[es] + std::string("：") + statstr);
                    } else {
                        height_preview_ -= 18;
                    }
                }

                inv_pos_preview_ = 1;
            }
        }
    }

    if (!oequip) {
        return;
    }

    const Equip &equip = *oequip;

    int32_t item_id = equip.get_item_id();

    const EquipData &equipdata = EquipData::get(item_id);
    const ItemData &itemdata = equipdata.get_itemdata();

    height_ = 540;

    item_icon_ = itemdata.get_icon(false);

    for (auto &ms : requirements_) {
        can_equip_[ms] = stats.get_stat(ms) >= equipdata.get_reqstat(ms);
        std::string reqstr = std::to_string(equipdata.get_reqstat(ms));

        if (ms != MapleStat::Id::LEVEL) {
            reqstr.insert(0, 3 - reqstr.size(), '0');
        }

        req_stat_strings_[ms] = reqstr;
    }

    okjobs_.clear();

    switch (equipdata.get_reqstat(MapleStat::Id::JOB)) {
        case 0:
            okjobs_.push_back(0);
            okjobs_.push_back(1);
            okjobs_.push_back(2);
            okjobs_.push_back(3);
            okjobs_.push_back(4);
            okjobs_.push_back(5);
            can_equip_[MapleStat::Id::JOB] = true;
            break;
        case 1:
            okjobs_.push_back(1);
            can_equip_[MapleStat::Id::JOB] =
                (stats.get_stat(MapleStat::Id::JOB) / 100 == 1)
                || (stats.get_stat(MapleStat::Id::JOB) / 100 >= 20);
            break;
        case 2:
            okjobs_.push_back(2);
            can_equip_[MapleStat::Id::JOB] =
                stats.get_stat(MapleStat::Id::JOB) / 100 == 2;
            break;
        case 4:
            okjobs_.push_back(3);
            can_equip_[MapleStat::Id::JOB] =
                stats.get_stat(MapleStat::Id::JOB) / 100 == 3;
            break;
        case 8:
            okjobs_.push_back(4);
            can_equip_[MapleStat::Id::JOB] =
                stats.get_stat(MapleStat::Id::JOB) / 100 == 4;
            break;
        case 16:
            okjobs_.push_back(5);
            can_equip_[MapleStat::Id::JOB] =
                stats.get_stat(MapleStat::Id::JOB) / 100 == 5;
            break;
        default: can_equip_[MapleStat::Id::JOB] = false; break;
    }

    prank_ = equip.get_potrank();

    Color::Name potcolor = Color::Name::WHITE;
    std::string potstr;

    switch (prank_) {
        case Equip::Potential::POT_HIDDEN:
            potcolor = Color::Name::RED;
            potstr = "（未知潜能）";
            break;
        case Equip::Potential::POT_RARE:
            potstr = "（稀有潜能）";
            break;
        case Equip::Potential::POT_EPIC:
            potstr = "（史诗潜能）";
            break;
        case Equip::Potential::POT_UNIQUE:
            potstr = "（独特潜能）";
            break;
        case Equip::Potential::POT_LEGENDARY:
            potstr = "（传说潜能）";
            break;
        default: height_ -= 16; break;
    }

    if (prank_ != Equip::Potential::POT_NONE) {
        potflag_ = Text(Text::Font::A11M,
                        Text::Alignment::CENTER,
                        potcolor,
                        potstr,
                        potflag_w);
    }

    Color::Name namecolor;

    switch (equip.get_quality()) {
        case EquipQuality::Id::GREY: namecolor = Color::Name::LIGHTGREY; break;
        case EquipQuality::Id::ORANGE: namecolor = Color::Name::ORANGE; break;
        case EquipQuality::Id::BLUE: namecolor = Color::Name::MEDIUMBLUE; break;
        case EquipQuality::Id::VIOLET: namecolor = Color::Name::VIOLET; break;
        case EquipQuality::Id::GOLD: namecolor = Color::Name::YELLOW; break;
        default: namecolor = Color::Name::WHITE; break;
    }

    std::string namestr = itemdata.get_name();
    const int8_t reqGender = itemdata.get_gender();
    const bool female = stats.get_female();

    switch (reqGender) {
        case 0:  // Male
            namestr += "（男）";
            break;
        case 1:  // Female
            namestr += "（女）";
            break;
        case 2:  // Unisex
        default: break;
    }

    if (equip.get_level() > 0) {
        namestr.append(" (+");
        namestr.append(std::to_string(equip.get_level()));
        namestr.append(")");
    }

    name_ =
        Text(Text::Font::A12B, Text::Alignment::LEFT, namecolor, namestr, name_w);
    atkinc_ = Text(Text::Font::A11M,
                   Text::Alignment::RIGHT,
                   Color::Name::DUSTYGRAY,
                   "攻击提升",
                   atkinc_w);

    std::string desctext = itemdata.get_desc();
    has_desc_ = !desctext.empty();

    if (has_desc_) {
        desc_ = Text(Text::Font::A12M,
                     Text::Alignment::LEFT,
                     Color::Name::WHITE,
                     desctext,
                     desc_w);
        height_ += desc_.height() + 10;
    }

    category_ = Text(Text::Font::A11M,
                     Text::Alignment::LEFT,
                     Color::Name::WHITE,
                     "类型：" + equipdata.get_type());

    is_weapon_ = equipdata.is_weapon();

    if (is_weapon_) {
        const WeaponData &weapon = WeaponData::get(item_id);
        wepspeed_ = Text(Text::Font::A11M,
                         Text::Alignment::LEFT,
                         Color::Name::WHITE,
                         "攻击速度：" + weapon.getspeedstring());
    } else {
        height_ -= 18;
    }

    has_slots_ = (equip.get_slots() > 0) || (equip.get_level() > 0);

    if (has_slots_) {
        slots_ = Text(
            Text::Font::A11M,
            Text::Alignment::LEFT,
            Color::Name::WHITE,
            "剩余强化次数：" + std::to_string(equip.get_slots()));

        std::string vicious = std::to_string(equip.get_vicious());

        if (equip.get_vicious() > 1) {
            vicious.append("（上限）");
        }

        hammers_ = Text(Text::Font::A11M,
                        Text::Alignment::LEFT,
                        Color::Name::WHITE,
                        "强化锤使用次数：" + vicious);
    } else {
        height_ -= 36;
    }

    stat_labels_.clear();

    for (EquipStat::Id es = EquipStat::Id::STR; es <= EquipStat::Id::JUMP;
         es = static_cast<EquipStat::Id>(es + 1)) {
        if (equip.get_stat(es) > 0) {
            int16_t delta = equip.get_stat(es) - equipdata.get_defstat(es);
            std::string statstr = std::to_string(equip.get_stat(es));

            if (delta != 0) {
                statstr.append(" (");
                statstr.append((delta < 0) ? "-" : "+");
                statstr.append(std::to_string(abs(delta)) + ")");
            }

            stat_labels_[es] =
                Text(Text::Font::A11M,
                     Text::Alignment::LEFT,
                     Color::Name::WHITE,
                     EquipStat::names[es] + std::string("：") + statstr);
        } else {
            height_ -= 18;
        }
    }
}

bool EquipTooltip::build(NativeTooltip::List &list, Point<int16_t> pos) const {
    if (inv_pos_ == 0 && goods_id_ == 0) {
        return false;
    }

    build_preview(list, pos);

    // The same clamp the pass that used to draw this worked out, for the same reason: the pair of
    // panels is placed against the finger that opened it and has to come back inside the interface's
    // screen when the finger is near an edge - and the width it holds itself to is the width of both
    // panels when there are two.
    int16_t max_width = Constants::Constants::get().get_ui_viewwidth();
    int16_t max_height = Constants::Constants::get().get_ui_viewheight();

    // The controls are set into the foot of the panel, so the body art is stretched to carry them.
    // The lettering walks down from the top, so the room handed to them is room it never used.
    const int16_t panel_height = height_ + actions_height();

    int16_t cur_width = pos.x() + top_.width();
    int16_t cur_height = pos.y() + 36;

    if (inv_pos_preview_ == 1) {
        cur_width += top_.width();
    }

    if (inv_pos_preview_ == 1) {
        cur_height += (panel_height > height_preview_) ? panel_height
                                                       : height_preview_;
    } else {
        cur_height += panel_height;
    }

    int16_t adj_x = cur_width - max_width;
    int16_t adj_y = cur_height - max_height;

    if (adj_x > 0) {
        pos.shift_x(adj_x * -1);
    }

    if (adj_y > 0) {
        pos.shift_y(adj_y * -1);
    }

    // Every line below moves `pos` about; the control is laid against the corner the panel itself
    // ended up at, so that corner is kept before the walking starts.
    const Point<int16_t> topleft = pos;

    list.picture(top_, DrawArgument(pos), "Frame/top");
    list.picture(mid_,
                 DrawArgument(pos + Point<int16_t>(0, 13),
                              Point<int16_t>(0, panel_height)),
                 "Frame/line");
    list.picture(bot_,
                 DrawArgument(pos + Point<int16_t>(0, panel_height + 13)),
                 "Frame/bottom");
    list.picture(cover_, DrawArgument(pos), "Frame/cover");

    list.text(name_, pos + Point<int16_t>(17, 7));

    if (prank_ != Equip::Potential::POT_NONE) {
        list.text(potflag_, pos + Point<int16_t>(POTFLAG_X, 20));
        pos.shift_y(16);
    }

    pos.shift_y(44);

    list.picture(line_, DrawArgument(pos), "Frame/dotline");

    list.text(atkinc_, pos + Point<int16_t>(ATKINC_X, 4));
    list.picture(base_, DrawArgument(pos + Point<int16_t>(12, 10)), "ItemIcon/base");
    list.picture(type_[false], DrawArgument(pos + Point<int16_t>(12, 10)), "ItemIcon/old");
    list.icon(item_icon_, DrawArgument(pos + Point<int16_t>(18, 82), 2.0f, 2.0f));
    list.picture(potential_[prank_],
                 DrawArgument(pos + Point<int16_t>(12, 10)),
                 potential_names_[prank_]);
    list.picture(item_cover_, DrawArgument(pos + Point<int16_t>(12, 10)), "ItemIcon/cover");

    int16_t atkincnum = 0;
    std::string atkincstr = std::to_string(atkincnum);
    bool atkinc_pos = true;

    if (can_equip_[MapleStat::Id::JOB]) {
        if (atkincnum < 0) {
            atkincstr = "m" + atkincstr;
            atkinc_pos = false;
        } else if (atkincnum > 0) {
            atkincstr = "p" + atkincstr;
            atkinc_pos = true;
        } else {
            atkinc_pos = true;
        }
    } else {
        atkincstr = "m";
        atkinc_pos = false;
    }

    atkincset_[atkinc_pos].build(list,
                             atkincstr,
                             SUMMARY_W,
                             pos + Point<int16_t>(SUMMARY_X, 26));

    pos.shift_y(12);

    for (MapleStat::Id ms : requirements_) {
        Point<int16_t> reqpos = req_stat_positions_[ms];
        bool reqok = can_equip_[ms];
        list.picture(req_stat_textures_[ms][reqok],
                     DrawArgument(pos + reqpos),
                     req_stat_names_[ms][reqok]);

        if (ms != MapleStat::Id::LEVEL) {
            reqset_[reqok].build(list,
                                 req_stat_strings_[ms],
                                 6,
                                 pos + reqpos + Point<int16_t>(54, 0));
        } else {
            lvset_[reqok].build(list,
                                req_stat_strings_[ms],
                                6,
                                pos + reqpos + Point<int16_t>(54, 0));
        }
    }

    pos.shift_y(88);

    Point<int16_t> job_position(pos + Point<int16_t>(10, 14));
    list.picture(jobs_back_, DrawArgument(job_position), "Equip/Job/normal");

    const bool jobok = can_equip_[MapleStat::Id::JOB];

    for (const auto &jbit : okjobs_) {
        list.picture(jobs_[jobok].at(jbit),
                     DrawArgument(job_position),
                     std::string("Equip/Job/") + (jobok ? "enable/" : "disable/")
                         + std::to_string(jbit));
    }

    list.picture(line_,
                 DrawArgument(pos + Point<int16_t>(0, 47)),
                 "Frame/dotline");

    pos.shift_y(49);

    int16_t stat_x = STAT_X;
    int16_t stat_y = 15;

    list.text(category_, pos + Point<int16_t>(stat_x, 0));

    pos.shift_y(stat_y);

    if (is_weapon_) {
        list.text(wepspeed_, pos + Point<int16_t>(stat_x, 0));
        pos.shift_y(stat_y);
    }

    for (const Text &label : stat_labels_.values()) {
        if (label.empty()) {
            continue;
        }

        list.text(label, pos + Point<int16_t>(stat_x, 0));
        pos.shift_y(stat_y);
    }

    if (has_slots_) {
        list.text(slots_, pos + Point<int16_t>(stat_x, 0));
        pos.shift_y(stat_y);
        list.text(hammers_, pos + Point<int16_t>(stat_x, 0));
        pos.shift_y(stat_y);
    }

    if (has_desc_) {
        pos.shift_y(13);
        list.picture(line_, DrawArgument(pos), "Frame/dotline");
        list.text(desc_, pos + Point<int16_t>(DESC_X, 3));
    }

    // Into the foot of the panel's body, at its own width, so the controls read as belonging to this
    // piece and not to whatever the preview beside it is about.
    build_actions(list,
                  Rectangle<int16_t>(topleft + Point<int16_t>(0, 13),
                                     topleft + Point<int16_t>(top_.width(),
                                                              13 + panel_height)));

    return true;
}

void EquipTooltip::build_preview(NativeTooltip::List &list, Point<int16_t> pos) const {
    if (inv_pos_preview_ == 0) {
        return;
    }

    // The piece already worn sits to the right of the piece under the finger, at its own width away.
    pos.shift_x(top_.width());

    int16_t max_width = Constants::Constants::get().get_ui_viewwidth();
    int16_t max_height = Constants::Constants::get().get_ui_viewheight();
    int16_t cur_width = pos.x() + top_.width();
    int16_t cur_height = pos.y() + height_preview_ + 36;

    int16_t adj_x = cur_width - max_width;
    int16_t adj_y = cur_height - max_height;

    if (adj_x > 0) {
        pos.shift_x(adj_x * -1);
    }

    if (adj_y > 0) {
        pos.shift_y(adj_y * -1);
    }

    list.picture(top_, DrawArgument(pos), "Frame/top");
    list.picture(mid_,
                 DrawArgument(pos + Point<int16_t>(0, 13),
                              Point<int16_t>(0, height_preview_)),
                 "Frame/line");
    list.picture(bot_,
                 DrawArgument(pos + Point<int16_t>(0, height_preview_ + 13)),
                 "Frame/bottom");
    list.picture(cover_, DrawArgument(pos), "Frame/cover");

    list.text(name_preview_, pos + Point<int16_t>(17, 7));

    if (prank_preview_ != Equip::Potential::POT_NONE) {
        list.text(potflag_preview_, pos + Point<int16_t>(POTFLAG_X, 20));
        pos.shift_y(16);
    }

    pos.shift_y(44);

    list.picture(line_, DrawArgument(pos), "Frame/dotline");

    list.text(atkinc_preview_, pos + Point<int16_t>(ATKINC_X, 4));
    list.picture(base_, DrawArgument(pos + Point<int16_t>(12, 10)), "ItemIcon/base");
    list.picture(type_[false], DrawArgument(pos + Point<int16_t>(12, 10)), "ItemIcon/old");
    list.icon(item_icon_preview_,
              DrawArgument(pos + Point<int16_t>(18, 82), 2.0f, 2.0f));
    list.picture(potential_[prank_preview_],
                 DrawArgument(pos + Point<int16_t>(12, 10)),
                 potential_names_[prank_preview_]);
    list.picture(item_cover_, DrawArgument(pos + Point<int16_t>(12, 10)), "ItemIcon/cover");

    int16_t atkincnum = 0;
    std::string atkincstr = std::to_string(atkincnum);
    bool atkinc_pos = true;

    if (can_equip_preview_[MapleStat::Id::JOB]) {
        if (atkincnum < 0) {
            atkincstr = "m" + atkincstr;
            atkinc_pos = false;
        } else if (atkincnum > 0) {
            atkincstr = "p" + atkincstr;
            atkinc_pos = true;
        } else {
            atkinc_pos = true;
        }
    } else {
        atkincstr = "m";
        atkinc_pos = false;
    }

    atkincset_[atkinc_pos].build(list,
                             atkincstr,
                             SUMMARY_W,
                             pos + Point<int16_t>(SUMMARY_X, 26));

    pos.shift_y(12);

    for (MapleStat::Id ms : requirements_) {
        Point<int16_t> reqpos = req_stat_positions_[ms];
        bool reqok = can_equip_preview_[ms];
        list.picture(req_stat_textures_[ms][reqok],
                     DrawArgument(pos + reqpos),
                     req_stat_names_[ms][reqok]);

        if (ms != MapleStat::Id::LEVEL) {
            reqset_[reqok].build(list,
                                 req_stat_strings_preview_[ms],
                                 6,
                                 pos + reqpos + Point<int16_t>(54, 0));
        } else {
            lvset_[reqok].build(list,
                                req_stat_strings_preview_[ms],
                                6,
                                pos + reqpos + Point<int16_t>(54, 0));
        }
    }

    pos.shift_y(88);

    Point<int16_t> job_position(pos + Point<int16_t>(10, 14));
    list.picture(jobs_back_, DrawArgument(job_position), "Equip/Job/normal");

    const bool jobok = can_equip_preview_[MapleStat::Id::JOB];

    for (const auto &jbit : okjobs_preview_) {
        list.picture(jobs_[jobok].at(jbit),
                     DrawArgument(job_position),
                     std::string("Equip/Job/") + (jobok ? "enable/" : "disable/")
                         + std::to_string(jbit));
    }

    list.picture(line_,
                 DrawArgument(pos + Point<int16_t>(0, 47)),
                 "Frame/dotline");

    pos.shift_y(49);

    int16_t stat_x = STAT_X;
    int16_t stat_y = 15;

    list.text(category_preview_, pos + Point<int16_t>(stat_x, 0));

    pos.shift_y(stat_y);

    if (is_weapon_preview_) {
        list.text(wepspeed_preview_, pos + Point<int16_t>(stat_x, 0));
        pos.shift_y(stat_y);
    }

    for (const Text &label : stat_labels_preview_.values()) {
        if (label.empty()) {
            continue;
        }

        list.text(label, pos + Point<int16_t>(stat_x, 0));
        pos.shift_y(stat_y);
    }

    if (has_slots_preview_) {
        list.text(slots_preview_, pos + Point<int16_t>(stat_x, 0));
        pos.shift_y(stat_y);
        list.text(hammers_preview_, pos + Point<int16_t>(stat_x, 0));
        pos.shift_y(stat_y);
    }

    if (has_desc_preview_) {
        pos.shift_y(-4);
        list.picture(line_, DrawArgument(pos), "Frame/dotline");
        list.text(desc_preview_, pos + Point<int16_t>(DESC_X, 8));
    }
}
}  // namespace ms