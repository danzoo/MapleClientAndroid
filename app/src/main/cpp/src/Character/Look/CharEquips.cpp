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
#include "CharEquips.h"

namespace ms {
CharEquips::CharEquips() {
    for (auto iter : clothes_) {
        iter.second = nullptr;
    }
}

void CharEquips::draw(EquipSlot::Id slot,
                      Stance::Id stance,
                      Clothing::Layer layer,
                      uint8_t frame,
                      const DrawArgument &args) const {
    if (const Clothing *cloth = clothes_[slot]) {
        cloth->draw(stance, layer, frame, args);
    }
}

void CharEquips::add_equip(int32_t itemid, const BodyDrawInfo &drawinfo) {
    if (itemid <= 0) {
        return;
    }

    auto iter = cloth_cache_.find(itemid);

    if (iter == cloth_cache_.end()) {
        iter = cloth_cache_
                   .emplace(std::piecewise_construct,
                            std::forward_as_tuple(itemid),
                            std::forward_as_tuple(itemid, drawinfo))
                   .first;
    }

    const Clothing &cloth = iter->second;

    EquipSlot::Id slot = cloth.get_eqslot();
    clothes_[slot] = &cloth;
}

void CharEquips::remove_equip(EquipSlot::Id slot) {
    clothes_[slot] = nullptr;
}

bool CharEquips::is_visible(EquipSlot::Id slot) const {
    if (const Clothing *cloth = clothes_[slot]) {
        return cloth->is_transparent() == false;
    }

    return false;
}

bool CharEquips::comparelayer(EquipSlot::Id slot,
                              Stance::Id stance,
                              Clothing::Layer layer) const {
    if (const Clothing *cloth = clothes_[slot]) {
        return cloth->contains_layer(stance, layer);
    }

    return false;
}

bool CharEquips::has_overall() const {
    return get_equip(EquipSlot::Id::TOP) / 10000 == 105;
}

bool CharEquips::has_weapon() const {
    return get_weapon() != 0;
}

bool CharEquips::is_twohanded() const {
    if (const Clothing *weapon = clothes_[EquipSlot::Id::WEAPON]) {
        return weapon->is_twohanded();
    }

    return false;
}

CharEquips::CapType CharEquips::getcaptype() const {
    if (const Clothing *cap = clothes_[EquipSlot::Id::HAT]) {
        const std::string &vslot = cap->get_vslot();

        // vslot lists the visual regions the cap covers (H1..H6 hair, A* accessories). The old
        // exact-string table knew three hats only; everything else - CpH1H4, the full helmets -
        // fell through to NONE and the cap layer was never drawn at all. Classify by the hair
        // regions instead: covering every main region hides the hair, covering the top (H1) puts
        // the cap over the hair, and a band leaving the top free lets the hair fall over the cap.
        const bool top = vslot.find("H1") != std::string::npos;
        const bool full = top
            && vslot.find("H2") != std::string::npos
            && vslot.find("H3") != std::string::npos
            && vslot.find("H4") != std::string::npos
            && vslot.find("H5") != std::string::npos;

        if (full) {
            return CharEquips::CapType::FULL_COVER;
        }

        if (top) {
            return CharEquips::CapType::HALF_COVER;
        }

        if (!vslot.empty()) {
            return CharEquips::CapType::HEADBAND;
        }
    }

    return CharEquips::CapType::NONE;
}

Stance::Id CharEquips::adjust_stance(Stance::Id stance) const {
    if (const Clothing *weapon = clothes_[EquipSlot::Id::WEAPON]) {
        switch (stance) {
            case Stance::Id::STAND1:
            case Stance::Id::STAND2: return weapon->get_stand();
            case Stance::Id::WALK1:
            case Stance::Id::WALK2: return weapon->get_walk();
            default: return stance;
        }
    } else {
        return stance;
    }
}

int32_t CharEquips::get_equip(EquipSlot::Id slot) const {
    if (const Clothing *cloth = clothes_[slot]) {
        return cloth->get_id();
    }

    return 0;
}

int32_t CharEquips::get_weapon() const {
    return get_equip(EquipSlot::Id::WEAPON);
}

std::unordered_map<int32_t, Clothing> CharEquips::cloth_cache_;
}  // namespace ms