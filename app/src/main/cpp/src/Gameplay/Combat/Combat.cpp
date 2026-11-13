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
#include "Combat.h"

#include <algorithm>
#include <cmath>

#include "../../Character/SkillId.h"
#include "../../IO/Messages.h"
#include "../../Net/Packets/AttackAndSkillPackets.h"
#include "../../Net/Packets/GameplayPackets.h"
#include "../Physics/FootholdTree.h"

namespace ms {
namespace {
auto fn_attack = []<typename... T>(T && ...args) {
    AttackPacket(std::forward<T>(args)...).dispatch();
};
auto fn_damage_reactor = []<typename... T>(T && ...args) {
    DamageReactorPacket(std::forward<T>(args)...).dispatch();
};
auto fn_use_skill = []<typename... T>(T && ...args) {
    UseSkillPacket(std::forward<T>(args)...).dispatch();
};
}  // namespace

Combat::Combat(Player &player,
               MapChars &chars,
               MapMobs &mobs,
               MapReactors &reactors,
               const FootholdTree &terrain) :
    player_(player),
    chars_(chars),
    mobs_(mobs),
    reactors_(reactors),
    terrain_(terrain),
    attack_results_([&](const AttackResult &attack) { apply_attack(attack); }),
    bullet_effects_(
        [&](const BulletEffect &effect) { apply_bullet_effect(effect); }),
    damage_effects_(
        [&](const DamageEffect &effect) { apply_damage_effect(effect); }) {}

void Combat::start_preload() {
    if (preloading_) {
        return;
    }

    auto ids = player_.get_skills().collect_active_ids();

    if (ids.empty()) {
        return;
    }

    preload_queue_.assign(ids.begin(), ids.end());
    preloading_ = true;
}

void Combat::draw(double viewx, double viewy, float alpha) const {
    for (const auto &bullet_eff : bullets_) {
        bullet_eff.bullet.draw(viewx, viewy, alpha);
    }

    for (const auto &dmg_eff : damage_numbers_) {
        dmg_eff.draw(viewx, viewy, alpha);
    }
}

void Combat::update() {
    attack_results_.update();
    bullet_effects_.update();
    damage_effects_.update();

    bullets_.remove_if([&](BulletEffect &mb) {
        int32_t target_oid = mb.damageeffect.target_oid;

        if (mobs_.contains(target_oid)) {
            mb.target = mobs_.get_mob_body_position(target_oid);
            Bullet::Flight flight = mb.bullet.update(mb.target);

            switch (flight) {
                case Bullet::Flight::ARRIVED:
                    apply_damage_effect(mb.damageeffect);
                    break;
                case Bullet::Flight::FLYING:
                    // A projectile that is still travelling counts as a blow this mob has
                    // coming for it, so a death reported over the network keeps waiting until
                    // the arrow lands. A shot stopped by a wall renews nothing, so the pending
                    // hit expires on its own.
                    mobs_.expect_hit(target_oid, 2 * Constants::TIMESTEP);
                    break;
                case Bullet::Flight::BLOCKED: break;
            }

            return flight != Bullet::Flight::FLYING;
        }
        return mb.bullet.update(mb.target) != Bullet::Flight::FLYING;
    });

    damage_numbers_.remove_if([](DamageNumber &dn) { return dn.update(); });

    // Process skill preload queue within time budget
    if (preloading_) {
        auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(PRELOAD_BUDGET_MS);

        while (!preload_queue_.empty()
               && std::chrono::steady_clock::now() < deadline) {
            int32_t id = preload_queue_.front();
            preload_queue_.pop_front();
            get_move(id);
        }

        if (preload_queue_.empty()) {
            preloading_ = false;
        }
    }
}

void Combat::use_move(int32_t move_id) {
    if (!player_.can_attack()) {
        return;
    }

    const SpecialMove &move = get_move(move_id);
    SpecialMove::ForbidReason reason = player_.can_use(move);
    Weapon::Type weapontype = player_.get_stats().get_weapontype();

    if (reason == SpecialMove::ForbidReason::FBR_NONE) {
        apply_move(move);
    } else {
        ForbidSkillMessage(reason, weapontype).drop();
    }
}

void Combat::apply_move(const SpecialMove &move) {
    if (move.is_attack()) {
        Attack attack = player_.prepare_attack(move.is_skill());

        move.apply_useeffects(player_);
        move.apply_actions(player_, attack.type);

        player_.set_afterimage(move.get_id());

        move.apply_stats(player_, attack);

        Point<int16_t> origin = attack.origin;
        Rectangle<int16_t> range = attack.range;
        auto hrange = static_cast<int16_t>(range.left() * attack.hrange);

        if (attack.toleft) {
            range = { static_cast<int16_t>(origin.x() + hrange),
                      static_cast<int16_t>(origin.x() + range.right()),
                              static_cast<int16_t>(origin.y() + range.top()),
                                      static_cast<int16_t>(origin.y() + range.bottom()) };
        } else {
            range = { static_cast<int16_t>(origin.x() - range.right()),
                    static_cast<int16_t>(origin.x() - hrange),
                            static_cast<int16_t>(origin.y() + range.top()),
                                    static_cast<int16_t>(origin.y() + range.bottom()) };
        }

        // This approach should also make it easier to implement PvP
        uint8_t mobcount = attack.mobcount;
        AttackResult result = attack;

        // The projectile leaves from the muzzle, so that is where the line to a target is
        // judged from, and only an attack that flies something can be stopped by a wall.
        Point<int16_t> muzzle = Bullet::muzzle(origin, attack.toleft);
        const bool projectile = attack.bullet != 0;

        MapObjects *mob_objs = mobs_.get_mobs();
        MapObjects *reactor_objs = reactors_.get_reactors();

        std::vector<int32_t> mob_targets =
            find_closest(mob_objs, range, origin, mobcount, true, muzzle, projectile);
        std::vector<int32_t> reactor_targets =
            find_closest(reactor_objs, range, origin, mobcount, false, muzzle, projectile);

        mobs_.send_attack(result, attack, mob_targets, mobcount);
        result.attacker = player_.get_oid();
        extract_effects(player_, move, result);

        apply_use_movement(move);
        apply_result_movement(move, result);

        result.hitdelay = player_.get_attackdelay(0);

        fn_attack(result);

        if (!reactor_targets.empty()) {
            if (auto reactor =
                    reactor_objs->get<Reactor>(reactor_targets.at(0))) {
                fn_damage_reactor(reactor->get().get_oid(),
                                  player_.get_position(),
                                  0,
                                  0);
            }
        }
    } else {
        move.apply_useeffects(player_);
        move.apply_actions(player_, Attack::Type::MAGIC);

        int32_t moveid = move.get_id();
        int32_t level = player_.get_skills().get_level(moveid);
        fn_use_skill(moveid, level);
    }
}

std::vector<int32_t> Combat::find_closest(MapObjects *objs,
                                          Rectangle<int16_t> range,
                                          Point<int16_t> origin,
                                          uint8_t objcount,
                                          bool use_mobs,
                                          Point<int16_t> muzzle,
                                          bool projectile) const {
    std::multimap<uint16_t, int32_t> distances;

    for (auto &mmo : *objs) {
        if (use_mobs) {
            const Mob *mob = static_cast<const Mob *>(mmo.second.get());

            if (mob && mob->is_alive() && mob->is_in_range(range)) {
                Point<int16_t> body = mob->get_body_position();
                Point<int16_t> wall;

                if (projectile && terrain_.blocks_line(muzzle, body, wall)) {
                    continue;
                }

                int32_t oid = mob->get_oid();
                uint16_t distance = mob->get_position().distance(origin);
                distances.emplace(distance, oid);
            }
        } else {
            // Assume Reactor
            const Reactor *reactor =
                static_cast<const Reactor *>(mmo.second.get());

            if (reactor && reactor->is_hittable()
                && reactor->is_in_range(range)) {
                Point<int16_t> pos = reactor->get_position();
                Point<int16_t> wall;

                if (projectile && terrain_.blocks_line(muzzle, pos, wall)) {
                    continue;
                }

                int32_t oid = reactor->get_oid();
                uint16_t distance = pos.distance(origin);
                distances.emplace(distance, oid);
            }
        }
    }

    std::vector<int32_t> targets;

    for (auto &iter : distances) {
        if (targets.size() >= objcount) {
            break;
        }

        targets.push_back(iter.second);
    }

    return targets;
}

void Combat::apply_use_movement(const SpecialMove &move) {
    switch (move.get_id()) {
        case SkillId::Id::TELEPORT_FP:
        case SkillId::Id::IL_TELEPORT:
        case SkillId::Id::PRIEST_TELEPORT:
        case SkillId::Id::FLASH_JUMP:
        default: break;
    }
}

void Combat::apply_result_movement(const SpecialMove &move,
                                   const AttackResult &result) {
    switch (move.get_id()) {
        case SkillId::Id::RUSH_HERO:
        case SkillId::Id::RUSH_PALADIN:
        case SkillId::Id::RUSH_DK: apply_rush(result); break;
        default: break;
    }
}

void Combat::apply_rush(const AttackResult &result) {
    if (result.mobcount == 0) {
        return;
    }

    Point<int16_t> mob_position = mobs_.get_mob_position(result.last_oid);
    int16_t targetx = mob_position.x();
    player_.rush(targetx);
}

void Combat::apply_bullet_effect(const BulletEffect &effect) {
    bullets_.push_back(effect);

    if (bullets_.back().bullet.settarget(effect.target)) {
        apply_damage_effect(effect.damageeffect);
        bullets_.pop_back();
    }
}

void Combat::apply_damage_effect(const DamageEffect &effect) {
    Point<int16_t> head_position =
        mobs_.get_mob_head_position(effect.target_oid);
    damage_numbers_.push_back(effect.number);
    damage_numbers_.back().set_x(head_position.x());

    const SpecialMove &move = get_move(effect.move_id);
    mobs_.apply_damage(effect.target_oid,
                       effect.damage,
                       effect.toleft,
                       effect.user,
                       move);
}

void Combat::push_attack(const AttackResult &attack) {
    attack_results_.push(400, attack);
}

void Combat::apply_attack(const AttackResult &attack) {
    if (auto ouser = chars_.get_char(attack.attacker)) {
        OtherChar &user = *ouser;
        user.update_skill(attack.skill, attack.level);
        user.update_speed(attack.speed);

        const SpecialMove &move = get_move(attack.skill);
        move.apply_useeffects(user);

        if (Stance::Id stance = Stance::by_id(attack.stance)) {
            user.attack(stance);
        } else {
            move.apply_actions(user, attack.type);
        }

        user.set_afterimage(attack.skill);

        extract_effects(user, move, attack);
    }
}

void Combat::extract_effects(const Char &user,
                             const SpecialMove &move,
                             const AttackResult &result) {
    AttackUser attack_user = { user.get_skilllevel(move.get_id()),
                               user.get_level(),
                               user.is_twohanded(),
                               !result.toleft };

    // The nearest target is struck on the animation's own frame, the farther ones wait in proportion
    // to how much farther they are, so the blows of one area attack land in turn instead of all at
    // once. damage_lines is unordered, hence the sort.
    Point<int16_t> from = user.get_position();
    std::vector<std::pair<int32_t, float>> ranges;

    for (const auto &[oid, vec] : result.damage_lines) {
        if (mobs_.contains(oid)) {
            Point<int16_t> delta = mobs_.get_mob_position(oid) - from;
            ranges.emplace_back(oid, std::hypot(delta.x(), delta.y()));
        }
    }

    std::sort(ranges.begin(), ranges.end(),
              [](const auto &a, const auto &b) { return a.second < b.second; });

    std::vector<std::pair<int32_t, uint16_t>> order;

    if (!ranges.empty()) {
        float nearest = ranges.front().second;
        float span = ranges.back().second - nearest;

        for (const auto &[oid, range] : ranges) {
            uint16_t wait = span > 0.0f
                ? static_cast<uint16_t>(Constants::MOB_HIT_SPREAD_MS * (range - nearest) / span)
                : 0;
            order.emplace_back(oid, wait);
        }
    }

    if (result.bullet) {
        Bullet bullet { move.get_bullet(user, result.bullet),
                        user.get_position(),
                        result.toleft,
                        terrain_ };

        for (const auto &[oid, wait] : order) {
            const auto &vec = result.damage_lines.at(oid);
            std::vector<DamageNumber> numbers = place_numbers(oid, vec);
            Point<int16_t> body = mobs_.get_mob_body_position(oid);

            size_t i = 0;

            for (auto &number : numbers) {
                DamageEffect effect {
                    attack_user,   number, vec[i].first,
                    result.toleft, oid,    move.get_id()
                };

                uint16_t hitdelay = user.get_attackdelay(i) + wait;
                mobs_.expect_hit(oid, hitdelay);

                bullet_effects_.emplace(hitdelay,
                                        std::move(effect),
                                        bullet,
                                        body);
                i++;
            }
        }

        if (result.damage_lines.empty()) {
            int16_t xshift = result.toleft ? -400 : 400;
            Point<int16_t> target =
                user.get_position() + Point<int16_t>(xshift, -26);

            for (int i = 0; i < result.hitcount; i++) {
                DamageEffect effect { attack_user, {}, 0, false, 0, 0 };
                bullet_effects_.emplace(user.get_attackdelay(i),
                                        std::move(effect),
                                        bullet,
                                        target);
            }
        }
    } else {
        for (const auto &[oid, wait] : order) {
            const auto &vec = result.damage_lines.at(oid);
            std::vector<DamageNumber> numbers = place_numbers(oid, vec);

            size_t i = 0;

            for (auto &number : numbers) {
                uint16_t hitdelay = user.get_attackdelay(i) + wait;
                mobs_.expect_hit(oid, hitdelay);

                damage_effects_.emplace(hitdelay,
                                        attack_user,
                                        number,
                                        vec[i].first,
                                        result.toleft,
                                        oid,
                                        move.get_id());

                i++;
            }
        }
    }
}

std::vector<DamageNumber> Combat::place_numbers(
    int32_t oid,
    const std::vector<std::pair<int32_t, bool>> &damage_lines) {
    std::vector<DamageNumber> numbers;
    int16_t head = mobs_.get_mob_head_position(oid).y();

    for (const auto &[amount, is_critical] : damage_lines) {
        DamageNumber::Type type = is_critical ? DamageNumber::Type::CRITICAL
                                              : DamageNumber::Type::NORMAL;
        numbers.emplace_back(type, amount, head);

        head -= DamageNumber::row_height(is_critical);
    }

    return numbers;
}

void Combat::show_buff(int32_t cid, int32_t skillid, int8_t level) {
    if (auto ouser = chars_.get_char(cid)) {
        OtherChar &user = *ouser;
        user.update_skill(skillid, level);

        const SpecialMove &move = get_move(skillid);
        move.apply_useeffects(user);
        move.apply_actions(user, Attack::Type::MAGIC);
    }
}

void Combat::show_player_buff(int32_t skillid) {
    get_move(skillid).apply_useeffects(player_);
}

void Combat::show_affected_by_buff(int32_t cid, int32_t skillid, int8_t level) {
    if (auto ouser = chars_.get_char(cid)) {
        OtherChar &user = *ouser;
        user.update_skill(skillid, level);

        const SpecialMove &move = get_move(skillid);
        move.apply_affected_effects(user);
    }
}

void Combat::show_player_affected_by_buff(int32_t skillid) {
    get_move(skillid).apply_affected_effects(player_);
}

void Combat::give_foreign_buff(int32_t cid, int32_t skillid, int8_t level) {
    if (auto ouser = chars_.get_char(cid)) {
        OtherChar &user = *ouser;
        user.update_skill(skillid, level);

        const SpecialMove &move = get_move(skillid);
        user.add_recurring_effect(
            1,
            img::wz::skill["MobSkill.img"][skillid]["level"][level]["affected"],
            1);
    }
}

void Combat::show_player_disease(int32_t skillid, int8_t level) {
    player_.add_recurring_effect(
        1,
        img::wz::skill["MobSkill.img"][skillid]["level"][level]["affected"],
        1);
}

const SpecialMove &Combat::get_move(int32_t move_id) {
    if (move_id == 0) {
        return regular_attack_;
    }

    auto iter = skills_.find(move_id);

    if (iter == skills_.end()) {
        iter = skills_.emplace(move_id, move_id).first;
    }

    return iter->second;
}
}  // namespace ms