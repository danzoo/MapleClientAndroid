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
#include "EquipStat.h"

namespace ms::EquipStat {
Id by_id(size_t id) {
    return static_cast<Id>(id);
}

int32_t value_of(Id value) {
    return value;
}

std::array<const char *, Id::LENGTH> names = {
    "力量",
    "敏捷",
    "智力",
    "幸运",
    "最大HP",
    "最大MP",
    "攻击力",
    "魔法攻击力",
    "防御力",
    "魔法防御力",
    "命中率",
    "回避率",
    "攻击速度",
    "速度",
    "跳跃力"
};
}  // namespace ms::EquipStat
