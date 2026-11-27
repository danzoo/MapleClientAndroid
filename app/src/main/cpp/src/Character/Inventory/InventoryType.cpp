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
#include "InventoryType.h"

#include <array>
#include <iostream>

namespace ms {
InventoryType::Id InventoryType::by_item_id(int32_t item_id) {
    constexpr std::array<Id, 6> values_by_id = { NONE,  EQUIP, USE,
                                                 SETUP, ETC,   CASH };

    // Dedicated tabs: scrolls (204xxxx/234xxxx) and ores are subsets of USE/ETC, so they must be
    // matched before the generic prefix routing. Must mirror the server's ItemConstants routing.
    if (is_scroll_item(item_id)) {
        return Id::SCROLL;
    }
    if (is_ore_item(item_id)) {
        return Id::ORE;
    }

    int32_t prefix = item_id / 1000000;

    return (prefix > Id::NONE && prefix <= Id::CASH) ? values_by_id[prefix]
                                                     : Id::NONE;
}

bool InventoryType::is_scroll_item(int32_t item_id) {
    int32_t prefix = item_id / 10000;
    return prefix == 204 || prefix == 234;
}

bool InventoryType::is_ore_item(int32_t item_id) {
    return (item_id >= 4004000 && item_id <= 4004004)
        || (item_id >= 4010000 && item_id <= 4010007)
        || (item_id >= 4011000 && item_id <= 4011008)
        || (item_id >= 4020000 && item_id <= 4020008)
        || (item_id >= 4021000 && item_id <= 4021008)
        || item_id == 4001018 || item_id == 4001154 || item_id == 4001197;
}

InventoryType::Id InventoryType::by_value(int8_t value) {
    switch (value) {
        case -1: return Id::EQUIPPED;
        case 1: return Id::EQUIP;
        case 2: return Id::USE;
        case 3: return Id::SETUP;
        case 4: return Id::ETC;
        case 5: return Id::CASH;
        case 7: return Id::SCROLL;
        case 8: return Id::ORE;
        default: break;
    }

    std::cout << "Unknown InventoryType::Id value: [" << value << "]"
              << std::endl;

    return Id::NONE;
}
}  // namespace ms