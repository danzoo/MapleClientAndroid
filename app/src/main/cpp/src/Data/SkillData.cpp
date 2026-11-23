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
#include "SkillData.h"

#include <imgwz/wz.hpp>

#include "../Character/SkillId.h"
#include "StringHandling.h"

namespace ms {
SkillData::SkillData(int32_t id) {
    /// Locate sources
    std::string strid = string_format::extend_id(id, 7);
    std::string jobid = strid.substr(0, 3);
    img::node src = img::wz::skill[jobid + ".img"]["skill"][strid];
    img::node strsrc = img::wz::string["Skill.img"][strid];

    // Load icons
    icons_ = { src["icon"], src["iconDisabled"], src["iconMouseOver"] };

    // Load strings
    name_ = std::string(strsrc["name"]);
    desc_ = std::string(strsrc["desc"]);

    for (int level = 1; img::node sub = strsrc["h" + std::to_string(level)];
         level++) {
        levels_.emplace(level, sub);
    }

    /// Load stats
    img::node levelsrc = src["level"];

    for (const auto &sub : levelsrc) {
        float damage = (float)sub["damage"] / 100;
        int32_t matk = sub["mad"];
        int32_t fixdamage = sub["fixdamage"];
        int32_t mastery = sub["mastery"];
        uint8_t attackcount = (uint8_t)sub["attackCount"].get_integer(1);
        uint8_t mobcount = (uint8_t)sub["mobCount"].get_integer(1);
        uint8_t bulletcount = (uint8_t)sub["bulletCount"].get_integer(1);
        int16_t bulletcost =
            (int16_t)sub["bulletConsume"].get_integer(bulletcount);
        int32_t hpcost = sub["hpCon"];
        int32_t mpcost = sub["mpCon"];
        float chance = (float)sub["prop"].get_real(100.0) / 100;
        float critical = 0.0f;
        float ignoredef = 0.0f;
        float hrange = (float)sub["range"].get_real(100.0) / 100;
        Rectangle<int16_t> range = sub;
        int32_t level = string_conversion::or_default<int32_t>(sub.name(), -1);

        stats_.emplace(std::piecewise_construct,
                       std::forward_as_tuple(level),
                       std::forward_as_tuple(damage,
                                             matk,
                                             fixdamage,
                                             mastery,
                                             attackcount,
                                             mobcount,
                                             bulletcount,
                                             bulletcost,
                                             hpcost,
                                             mpcost,
                                             chance,
                                             critical,
                                             ignoredef,
                                             hrange,
                                             range));
    }

    element_ = std::string(src["elemAttr"]);

    if (jobid == "900" || jobid == "910") {
        req_weapon_ = Weapon::Type::NONE;
    } else {
        req_weapon_ = Weapon::by_value(100 + (int32_t)src["weapon"]);
    }

    master_level_ = static_cast<int32_t>(stats_.size());
    passive_ = (id % 10000) / 1000 == 0;
    flags_ = flags_of(id);
    invisible_ = src["invisible"].get_bool();

    /// Load required skills
    img::node reqsrc = src["req"];

    for (const auto &sub : reqsrc) {
        int32_t skillid =
            string_conversion::or_default<int32_t>(sub.name(), -1);
        int32_t reqlv = sub.get_integer();

        req_skills_.emplace(skillid, reqlv);
    }
}

int32_t SkillData::flags_of(int32_t id) const {
    static const std::unordered_map<int32_t, int32_t> skill_flags = {
        // Beginner
        { SkillId::THREE_SNAILS, ATTACK },
        // Warrior
        { SkillId::POWER_STRIKE, ATTACK },
        { SkillId::SLASH_BLAST, ATTACK },
        // Fighter
        // Page
        // Crusader
        { SkillId::SWORD_PANIC, ATTACK },
        { SkillId::AXE_PANIC, ATTACK },
        { SkillId::SWORD_COMA, ATTACK },
        { SkillId::AXE_COMA, ATTACK },
        // Hero
        { SkillId::RUSH_HERO, ATTACK },
        { SkillId::BRANDISH, ATTACK },
        // Page
        // White Knight
        { SkillId::CHARGE, ATTACK },
        // Paladin
        { SkillId::RUSH_PALADIN, ATTACK },
        { SkillId::BLAST, ATTACK },
        { SkillId::HEAVENS_HAMMER, ATTACK },
        // Spearman
        // Dragon Knight
        { SkillId::DRAGON_BUSTER, ATTACK },
        { SkillId::DRAGON_FURY, ATTACK },
        { SkillId::PA_BUSTER, ATTACK },
        { SkillId::PA_FURY, ATTACK },
        { SkillId::SACRIFICE, ATTACK },
        { SkillId::DRAGONS_ROAR, ATTACK },
        // Dark Knight
        { SkillId::RUSH_DK, ATTACK },
        // Mage
        { SkillId::ENERGY_BOLT, ATTACK | RANGED },
        { SkillId::MAGIC_CLAW, ATTACK | RANGED },
        // Bowman
        { SkillId::ARROW_BLOW, ATTACK | RANGED },
        { SkillId::DOUBLE_SHOT, ATTACK | RANGED },
        // F/P Mage
        { SkillId::SLOW_FP, ATTACK },
        { SkillId::FIRE_ARROW, ATTACK | RANGED },
        { SkillId::POISON_BREATH, ATTACK | RANGED },
        // F/P ArchMage
        { SkillId::EXPLOSION, ATTACK },
        { SkillId::POISON_MIST, ATTACK | RANGED },
        { SkillId::SEAL_FP, ATTACK },
        { SkillId::ELEMENT_COMPOSITION_FP, ATTACK | RANGED },
        //
        { SkillId::FIRE_DEMON, ATTACK },
        { SkillId::PARALYZE, ATTACK | RANGED },
        { SkillId::METEOR_SHOWER, ATTACK },
        // 其余职业的攻击技能按技能 id 直接列出（权威分类来自服务端 constants/skills）。
        // 只收录真正走攻击结算的主动技；带 damage 节点的 buff（精神力、属性_charge_、
        // 斗气集中、祝福）与召唤兽/变身/纯 debuff 一律不在此表，仍走 UseSkill 发包。
        // Crusader
        { 1111008, ATTACK },                  // 虎咆哮
        // F/P Archmage
        { 2121001, ATTACK | RANGED },         // 创世之破
        // I/L Wizard
        { 2201003, ATTACK },                  // 迟缓术
        { 2201004, ATTACK | RANGED },         // 冰冻术
        { 2201005, ATTACK | RANGED },         // 雷电术
        // I/L Mage
        { 2211002, ATTACK | RANGED },         // 冰咆哮
        { 2211003, ATTACK | RANGED },         // 落雷枪
        { 2211004, ATTACK },                  // 封印术
        { 2211006, ATTACK | RANGED },         // 冰雷合击
        // I/L Archmage
        { 2221001, ATTACK | RANGED },         // 创世之破
        { 2221003, ATTACK | RANGED },         // 冰凤球
        { 2221006, ATTACK | RANGED },         // 链环闪电
        { 2221007, ATTACK | RANGED },         // 落霜冰破
        // Cleric
        { 2301005, ATTACK | RANGED },         // 圣箭术
        // Priest
        { 2311004, ATTACK | RANGED },         // 圣光
        // Bishop
        { 2321001, ATTACK | RANGED },         // 创世之破
        { 2321007, ATTACK | RANGED },         // 光芒飞箭
        { 2321008, ATTACK | RANGED },         // 圣光普照
        // Hunter
        { 3101003, ATTACK | RANGED },         // 强弓
        { 3101005, ATTACK | RANGED },         // 爆炸箭
        // Ranger
        { 3111003, ATTACK | RANGED },         // 烈火箭
        { 3111004, ATTACK | RANGED },         // 箭雨
        { 3111006, ATTACK | RANGED },         // 箭扫射
        // Bowmaster
        { 3121003, ATTACK | RANGED },         // 飞龙冲击波
        { 3121004, ATTACK | RANGED },         // 暴风箭雨
        // Crossbowman
        { 3201003, ATTACK | RANGED },         // 强弩
        { 3201005, ATTACK | RANGED },         // 穿透箭
        // Sniper
        { 3211003, ATTACK | RANGED },         // 寒冰箭
        { 3211004, ATTACK | RANGED },         // 升龙弩
        { 3211006, ATTACK | RANGED },         // 箭扫射
        // Marksman
        { 3221001, ATTACK | RANGED },         // 穿透箭
        { 3221003, ATTACK | RANGED },         // 飞龙冲击波
        { 3221007, ATTACK | RANGED },         // 一击要害
        // Rogue
        { 4001334, ATTACK },                  // 二连击
        { 4001344, ATTACK | RANGED },         // 双飞斩
        // Assassin
        { 4101005, ATTACK | RANGED },         // 生命吸收
        // Night Lord
        { 4121004, ATTACK | RANGED },         // 忍者伏击
        { 4121008, ATTACK | RANGED },         // 忍者冲击
        // Bandit
        { 4201004, ATTACK },                  // 神通术
        { 4201005, ATTACK },                  // 回旋斩
        // Chief Bandit
        { 4211002, ATTACK },                  // 落叶斩
        { 4211004, ATTACK },                  // 分身术
        // Shadower
        { 4221001, ATTACK },                  // 暗杀
        { 4221004, ATTACK },                  // 忍者伏击
        { 4221007, ATTACK },                  // 一出双击
        // Pirate
        { 5001001, ATTACK },                  // 百裂拳
        { 5001002, ATTACK },                  // 半月踢
        { 5001003, ATTACK | RANGED },         // 双弹射击
        // Brawler
        { 5101002, ATTACK },                  // 回马
        { 5101003, ATTACK },                  // 升龙连击
        { 5101004, ATTACK },                  // 贯骨击
        // Marauder
        { 5111002, ATTACK },                  // 能量爆破
        { 5111004, ATTACK },                  // 能量耗转
        { 5111006, ATTACK },                  // 碎石乱击
        // Buccaneer
        { 5121001, ATTACK },                  // 潜龙出渊
        { 5121002, ATTACK },                  // 超能量
        { 5121004, ATTACK },                  // 金手指
        { 5121005, ATTACK },                  // 索命
        { 5121007, ATTACK },                  // 光速拳
        // Gunslinger
        { 5201001, ATTACK | RANGED },         // 快枪手
        { 5201002, ATTACK | RANGED },         // 投弹攻击
        { 5201004, ATTACK | RANGED },         // 迷惑射击
        { 5201006, ATTACK | RANGED },         // 激退射杀
        // Outlaw
        { 5211004, ATTACK | RANGED },         // 烈焰喷射
        { 5211005, ATTACK | RANGED },         // 寒冰喷射
        { 5211006, ATTACK | RANGED },         // 导航
        // Corsair
        { 5221003, ATTACK | RANGED },         // 地毯式空袭
        { 5221004, ATTACK | RANGED },         // 金属风暴
        { 5221007, ATTACK | RANGED },         // 急速射
        { 5221008, ATTACK | RANGED }          // 重量炮击
    };

    auto iter = skill_flags.find(id);

    if (iter == skill_flags.end()) {
        return NONE;
    }

    return iter->second;
}

bool SkillData::is_passive() const {
    return passive_;
}

bool SkillData::is_attack() const {
    return !passive_ && (flags_ & ATTACK);
}

bool SkillData::is_invisible() const {
    return invisible_;
}

int32_t SkillData::get_masterlevel() const {
    return master_level_;
}

Weapon::Type SkillData::get_required_weapon() const {
    return req_weapon_;
}

const SkillData::Stats &SkillData::get_stats(int32_t level) const {
    auto iter = stats_.find(level);

    if (iter == stats_.end()) {
        static constexpr Stats null_stats = Stats(0.0f,
                                                  0,
                                                  0,
                                                  0,
                                                  0,
                                                  0,
                                                  0,
                                                  0,
                                                  0,
                                                  0,
                                                  0.0f,
                                                  0.0f,
                                                  0.0f,
                                                  0.0f,
                                                  Rectangle<int16_t>());

        return null_stats;
    }

    return iter->second;
}

const std::string &SkillData::get_name() const {
    return name_;
}

const std::string &SkillData::get_desc() const {
    return desc_;
}

const std::string &SkillData::get_level_desc(int32_t level) const {
    auto iter = levels_.find(level);

    if (iter == levels_.end()) {
        static const std::string null_level = "Missing level description.";

        return null_level;
    }
    return iter->second;
}

const Texture &SkillData::get_icon(Icon icon) const {
    return icons_[icon];
}

const std::unordered_map<int32_t, int32_t> &SkillData::get_reqskills() const {
    return req_skills_;
}
}  // namespace ms