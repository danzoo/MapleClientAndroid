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
#include <atomic>
#include <chrono>
#include <cmath>

#include "../../Character/SkillId.h"
#include "../../IO/Messages.h"
#include "../../Net/Packets/AttackAndSkillPackets.h"
#include "../../Net/Packets/GameplayPackets.h"
#include "AttackHitbox.h"

#ifdef MS_DEBUG_HITBOXES
#include <android/log.h>
#include "../../Graphics/GraphicsGL.h"
#endif

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

uint64_t next_hit_token() {
    // Never reuse a token when a map or Combat instance changes.
    static std::atomic<uint64_t> token { 0 };
    return token.fetch_add(1, std::memory_order_relaxed) + 1;
}
}  // namespace

Combat::Combat(Player &player,
               MapChars &chars,
               MapMobs &mobs,
               MapReactors &reactors) :
    player_(player),
    chars_(chars),
    mobs_(mobs),
    reactors_(reactors),
    attack_results_([&](const QueuedAttack &attack) { apply_attack(attack); }),
    bullet_effects_(
        [&](const BulletEffect &effect) { apply_bullet_effect(effect); }),
    damage_effects_(
        [&](const DamageEffect &effect) { apply_damage_effect(effect); }) {}

void Combat::clear() {
    attack_results_.clear();
    bullet_effects_.clear();
    damage_effects_.clear();
    bullets_.clear();
    damage_numbers_.clear();
#ifdef MS_DEBUG_HITBOXES
    debug_attack_hitbox_.reset();
    debug_targets_.clear();
#endif
}

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

#ifdef MS_DEBUG_HITBOXES
void Combat::draw_debug_hitboxes(double viewx, double viewy) const {
    for (const auto &entry : *mobs_.get_mobs()) {
        const auto *mob = static_cast<const Mob *>(entry.second.get());
        if (mob) {
            mob->draw_debug_hitbox(viewx, viewy);
        }
    }

    if (!player_.is_attacking() || !debug_attack_hitbox_
        || player_.get_stats().get_stat(MapleStat::Id::HP) == 0) {
        return;
    }

    const Color attack_color(1.0f, 0.2f, 0.15f, 0.9f);
    debug_attack_hitbox_->for_each_outline_edge(
        [viewx, viewy, &attack_color](Point<int32_t> from, Point<int32_t> to) {
            GraphicsGL::get().draw_debug_line(
                Point<double>(from.x() + viewx, from.y() + viewy),
                Point<double>(to.x() + viewx, to.y() + viewy), attack_color);
        });

    for (const auto &snapshot : debug_targets_) {
        Color color(0.6f, 0.6f, 0.6f, 0.9f);
        switch (snapshot.state) {
            case DebugTarget::State::SELECTED:
                color = Color(0.1f, 0.9f, 1.0f, 0.95f);
                break;
            case DebugTarget::State::TARGET_LIMIT:
            case DebugTarget::State::NOT_ALIVE:
                break;
        }

        const auto draw_edge = [viewx, viewy, &color](Point<int32_t> from,
                                                     Point<int32_t> to) {
            GraphicsGL::get().draw_debug_line(
                Point<double>(from.x() + viewx, from.y() + viewy),
                Point<double>(to.x() + viewx, to.y() + viewy), color);
        };
        AttackHitbox(snapshot.bounds).for_each_outline_edge(draw_edge);
    }
}
#endif

void Combat::update() {
#ifdef MS_DEBUG_HITBOXES
    if (!player_.is_attacking() || player_.get_stats().get_stat(MapleStat::Id::HP) == 0) {
        debug_attack_hitbox_.reset();
        debug_targets_.clear();
    }
#endif
    attack_results_.update();
    bullet_effects_.update();
    damage_effects_.update();

    bullets_.remove_if([&](BulletEffect &mb) {
        int32_t target_oid = mb.damageeffect.target_oid;

        if (mb.damageeffect.hit_token != 0
            && mobs_.has_pending_hit(target_oid, mb.damageeffect.hit_token)) {
            mb.target = mobs_.get_mob_body_position(target_oid);
            Bullet::Flight flight = mb.bullet.update(mb.target);

            switch (flight) {
                case Bullet::Flight::ARRIVED:
                    apply_damage_effect(mb.damageeffect);
                    break;
                case Bullet::Flight::FLYING:
                    mobs_.expect_hit(target_oid, 2 * Constants::TIMESTEP);
                    break;
            }

            return flight != Bullet::Flight::FLYING;
        }

        // The target died, faded or was replaced. Finish the visible flight toward its
        // last valid position, without reading DIE bounds or producing a corpse hit.
        mobs_.finish_hit(target_oid, mb.damageeffect.hit_token);
        mb.damageeffect.hit_token = 0;
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

    const SpecialMove *move = &get_move(move_id);
    const auto requested_reason = player_.can_use(*move, true);
    if (requested_reason != SpecialMove::FBR_NONE
        && requested_reason != SpecialMove::FBR_MPCOST
        && requested_reason != SpecialMove::FBR_BULLETCOST) {
        // Only resource/posture failures can degenerate. Do not bypass skill ownership,
        // weapon requirements, cooldowns or the ground requirement through a basic swing.
        ForbidSkillMessage(requested_reason, player_.get_weapontype()).drop();
        return;
    }

    // An attack the character has not the MP for swings instead, so an attack key never dead-presses.
    // The red line still says why the skill did not go off.
    if (move->is_attack()
        && player_.can_use(*move) == SpecialMove::ForbidReason::FBR_MPCOST) {
        ForbidSkillMessage(SpecialMove::ForbidReason::FBR_MPCOST,
                           player_.get_stats().get_weapontype())
            .drop();
        move = &get_move(0);
    }

    // Prone carries no skill swing either; the same key still means an attack, so it degenerates to
    // the basic one instead of being refused.
    if (move->is_attack() && move->is_skill() && player_.is_prone()) {
        move = &get_move(0);
    }

    SpecialMove::ForbidReason reason = player_.can_use(*move);
    const Weapon::Type weapontype = player_.get_weapontype();
    const bool bow = weapontype == Weapon::BOW || weapontype == Weapon::CROSSBOW;
    const bool ammo_weapon = weapontype == Weapon::BOW || weapontype == Weapon::CROSSBOW
                            || weapontype == Weapon::CLAW;
    bool close = move->is_attack() && SkillId::bow_melee(move->get_id());

    // Out of arrows/bolts/stars, a skill that needs them still warns why it did not fire even
    // though the key swings instead; the basic attack stays silent. display_message throttles
    // repeats per type (MESSAGE_COOLDOWN_), so holding the key does not spam the red line.
    if (ammo_weapon && move->is_skill() && reason == SpecialMove::FBR_BULLETCOST) {
        ForbidSkillMessage(SpecialMove::FBR_BULLETCOST, weapontype).drop();
    }

    if (bow && move->is_attack() && SkillId::bow_has_minimum_range(move->get_id())
        && !player_.can_shoot_bow_at_close_range()
        && (reason == SpecialMove::FBR_NONE || reason == SpecialMove::FBR_BULLETCOST)
        && has_close_bow_target()) {
        // Without the close-range unlock, use the basic move in its entirety: no skill MP,
        // multiplier, effect or projectile. With it, retain the shot and its resource checks.
        // Invalid/unlearned/cooling skills and locked airborne shots cannot enter this fallback.
        move = &get_move(0);
        close = true;
        reason = player_.can_use(*move);
        if (reason == SpecialMove::FBR_BULLETCOST) {
            reason = SpecialMove::FBR_NONE; // A weapon swing does not consume arrows.
        }
    }

    // Out of arrows, bolts or throwing stars, a bow/crossbow/claw still answers the attack key:
    // swing it as a degenerate melee attack instead of dead-pressing. prepare_attack turns the
    // empty projectile slot into a CLOSE swing; fall back to the basic move so the failed skill
    // spends no MP, multiplier or effect. Ownership/weapon/cooldown refusals never reach here.
    if (ammo_weapon && move->is_attack() && reason == SpecialMove::FBR_BULLETCOST) {
        move = &get_move(0);
        close = true;
        reason = SpecialMove::FBR_NONE;
    }

    if (reason == SpecialMove::ForbidReason::FBR_NONE) {
        apply_move(*move, close);
    } else {
        ForbidSkillMessage(reason, weapontype).drop();
    }
}

bool Combat::has_close_bow_target() const {
    const Point<int16_t> origin = player_.get_attack_origin();
    const bool toleft = !player_.getflip();
    // Candidate afterimage lt/rb share the physical feet anchor with the melee hitbox.
    // Preserve their intersection's offsets; only mirror horizontally for facing.
    Rectangle<int16_t> range = player_.get_look().get_degenerate_range();

    if (range.empty()) {
        return false;
    }

    if (toleft) {
        range = { static_cast<int16_t>(origin.x() + range.left()),
                  static_cast<int16_t>(origin.x() + range.right()),
                  static_cast<int16_t>(origin.y() + range.top()),
                  static_cast<int16_t>(origin.y() + range.bottom()) };
    } else {
        range = { static_cast<int16_t>(origin.x() - range.right()),
                  static_cast<int16_t>(origin.x() - range.left()),
                  static_cast<int16_t>(origin.y() + range.top()),
                  static_cast<int16_t>(origin.y() + range.bottom()) };
    }
    for (const auto &entry : *mobs_.get_mobs()) {
        const auto *mob = static_cast<const Mob *>(entry.second.get());

        if (mob && mob->is_alive() && mob->is_in_range(range)) {
            return true;
        }
    }

    return false;
}

void Combat::apply_move(const SpecialMove &move, bool close) {
    if (move.is_attack()) {
        Attack attack = player_.prepare_attack(move.is_skill(), close);

        move.apply_useeffects(player_);
        move.apply_actions(player_, attack.type);

        player_.set_afterimage(move.get_id());

        move.apply_stats(player_, attack);

        // All hit ranges share the physical feet anchor, independent of animation/action move.
        attack.origin = player_.get_attack_origin();

        // A projectile with nothing to hit runs to the far edge of this attack's own hit strip,
        // read after apply_stats: a skill's lt/rb from the img - 蜗牛投掷术 flies to its defined 464
        // and never rides the bow's bonus - scaled by the range percentage the same node carries,
        // or the weapon's straight-shot band for a plain shot, which 远程箭 lengthens.
        if (attack.bullet != 0) {
            attack.shootdistance = static_cast<int16_t>(
                std::max<int32_t>(static_cast<int32_t>(-attack.range.left() * attack.hrange),
                                  attack.range.right()));
        }

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

        const Point<int16_t> physical_origin = player_.get_position();
        const bool projectile = attack.bullet != 0;
        // Skills without img lt/rb and ordinary projectile attacks use a feet-origin cone
        // plus a full-height tail, including non-projectile skills with a fallback range.
        // Explicit img/afterimage rectangles retain their bounds, multipliers and facing.
        const bool tapered = !attack.preserve_rectangular_range
                             && (move.is_skill() || projectile);
        const AttackHitbox hitbox = tapered
            ? AttackHitbox::cone_with_rectangular_tail(
                  range, origin, attack.toleft, Constants::DEFAULT_ATTACK_TAPER_LENGTH)
            : AttackHitbox(range);

#ifdef MS_DEBUG_HITBOXES
        debug_attack_hitbox_ = hitbox;
#endif

        MapObjects *mob_objs = mobs_.get_mobs();
        MapObjects *reactor_objs = reactors_.get_reactors();

        std::vector<int32_t> mob_targets =
            find_closest(mob_objs, hitbox, physical_origin, mobcount, true);
        std::vector<int32_t> reactor_targets =
            find_closest(reactor_objs, hitbox, physical_origin, mobcount, false);

#ifdef MS_DEBUG_HITBOXES
        // No update has intervened since selection; freeze only intersecting world bounds
        // before send_attack can calculate damage or change a mob's state.
        debug_targets_.clear();
        for (const auto &entry : *mob_objs) {
            const auto *mob = static_cast<const Mob *>(entry.second.get());
            if (!mob) {
                continue;
            }
            const auto bounds = mob->get_debug_hit_bounds();
            if (!hitbox.overlaps(bounds)) {
                continue;
            }

            const int32_t oid = mob->get_oid();
            DebugTarget snapshot { bounds, DebugTarget::State::TARGET_LIMIT };
            const char *state = "TARGET_LIMIT";
            if (std::find(mob_targets.begin(), mob_targets.end(), oid) != mob_targets.end()) {
                snapshot.state = DebugTarget::State::SELECTED;
                state = "SELECTED";
            } else if (!mob->is_alive()) {
                snapshot.state = DebugTarget::State::NOT_ALIVE;
                state = "NOT_ALIVE";
            }
            debug_targets_.push_back(snapshot);
            __android_log_print(
                ANDROID_LOG_DEBUG, "HitboxDebug",
                "Selection oid=%d state=%s bounds=(left=%d,right=%d,top=%d,bottom=%d) "
                "skill=%d facing=%s",
                static_cast<int>(oid), state,
                static_cast<int>(bounds.left()), static_cast<int>(bounds.right()),
                static_cast<int>(bounds.top()), static_cast<int>(bounds.bottom()),
                static_cast<int>(move.get_id()), attack.toleft ? "left" : "right");
        }
#endif

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
#ifdef MS_DEBUG_HITBOXES
        debug_attack_hitbox_.reset();
        debug_targets_.clear();
#endif
        move.apply_useeffects(player_);
        move.apply_actions(player_, Attack::Type::MAGIC);

        int32_t moveid = move.get_id();
        int32_t level = player_.get_skills().get_level(moveid);
        fn_use_skill(moveid, level);
    }
}

std::vector<int32_t> Combat::find_closest(MapObjects *objs,
                                          const AttackHitbox &range,
                                          Point<int16_t> origin,
                                          uint8_t objcount,
                                          bool use_mobs) const {
    std::multimap<uint16_t, int32_t> distances;

    for (auto &mmo : *objs) {
        if (use_mobs) {
            const Mob *mob = static_cast<const Mob *>(mmo.second.get());

            if (mob && mob->is_alive() && mob->is_in_range(range)) {
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
    auto &bullet_effect = bullets_.back();
    auto &damage = bullet_effect.damageeffect;
    if (damage.hit_token != 0
        && !mobs_.has_pending_hit(damage.target_oid, damage.hit_token)) {
        mobs_.finish_hit(damage.target_oid, damage.hit_token);
        damage.hit_token = 0;
    }

    if (bullet_effect.bullet.settarget(effect.target)) {
        apply_damage_effect(damage);
        bullets_.pop_back();
    }
}

void Combat::apply_damage_effect(const DamageEffect &effect) {
    // This gate also covers near-muzzle instant hits and queued non-projectile damage.
    // is_alive() is intentionally not used: the server can report death before a lethal arrow lands.
    if (effect.hit_token == 0
        || !mobs_.has_pending_hit(effect.target_oid, effect.hit_token)) {
        mobs_.finish_hit(effect.target_oid, effect.hit_token);
        return;
    }

    Point<int16_t> head_position =
        mobs_.get_mob_head_position(effect.target_oid);
    damage_numbers_.push_back(effect.number);
    damage_numbers_.back().set_x(head_position.x());

    const SpecialMove &move = get_move(effect.move_id);
    mobs_.apply_damage(effect.target_oid,
                       effect.damage,
                       effect.toleft,
                       effect.user,
                       move,
                       effect.knockback);
    mobs_.finish_hit(effect.target_oid, effect.hit_token);
}

void Combat::push_attack(const AttackResult &attack) {
    QueuedAttack queued { attack, {} };
    for (const auto &[oid, lines] : attack.damage_lines) {
        for (size_t i = 0; i < lines.size(); ++i) {
            uint64_t token = next_hit_token();
            mobs_.queue_hit(oid, token, 400);
            queued.hit_tokens[oid].push_back(token);
        }
    }
    attack_results_.push(400, queued);
}

void Combat::apply_attack(const QueuedAttack &queued) {
    const AttackResult &attack = queued.result;
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

        extract_effects(user, move, attack, &queued.hit_tokens);
    } else {
        // The attacker left during the display delay; none of its queued hits will run.
        for (const auto &[oid, tokens] : queued.hit_tokens) {
            for (uint64_t token : tokens) {
                mobs_.finish_hit(oid, token);
            }
        }
    }
}

void Combat::extract_effects(const Char &user,
                             const SpecialMove &move,
                             const AttackResult &result,
                             const HitTokens *reserved_hits) {
    AttackUser attack_user = { user.get_skilllevel(move.get_id()),
                               user.get_level(),
                               user.is_twohanded(),
                               !result.toleft };

    // The nearest target is struck on the animation's own frame, the farther ones wait in proportion
    // to how much farther they are, so the blows of one area attack land in turn instead of all at
    // once. damage_lines is unordered, hence the sort.
    Point<int16_t> from = user.get_position();
    std::vector<std::pair<int32_t, float>> ranges;

    auto reserve_hit = [&](int32_t oid, size_t line, uint16_t delay) -> uint64_t {
        if (reserved_hits) {
            auto target = reserved_hits->find(oid);
            if (target == reserved_hits->end() || line >= target->second.size()) {
                return 0;
            }
            uint64_t token = target->second[line];
            if (!mobs_.has_pending_hit(oid, token)) {
                return 0;
            }
            mobs_.expect_hit(oid, delay);
            return token;
        }
        uint64_t token = next_hit_token();
        mobs_.queue_hit(oid, token, delay);
        return token;
    };

    for (const auto &[oid, vec] : result.damage_lines) {
        bool original_target = true;
        if (reserved_hits) {
            auto target = reserved_hits->find(oid);
            original_target = target != reserved_hits->end() && !target->second.empty()
                              && mobs_.has_pending_hit(oid, target->second.front());
        }
        if (original_target && mobs_.can_receive_hit(oid)) {
            Point<int16_t> delta = mobs_.get_mob_position(oid) - from;
            ranges.emplace_back(oid, std::hypot(delta.x(), delta.y()));

            int64_t total = 0;
            for (const auto &[amount, critical] : vec) {
                total += amount;
            }
            mobs_.note_damage(oid, result.attacker, static_cast<int32_t>(total));
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
        Animation bullet_anim = move.get_bullet(user, result.bullet);
        Point<int16_t> origin = user.get_position();

        for (const auto &[oid, wait] : order) {
            const auto &vec = result.damage_lines.at(oid);
            std::vector<DamageNumber> numbers = place_numbers(oid, vec);
            Point<int16_t> body = mobs_.get_mob_body_position(oid);

            size_t n = numbers.size();
            size_t i = 0;

            for (auto &number : numbers) {
                DamageEffect effect {
                    attack_user,   number, vec[i].first,
                    result.toleft, oid,    move.get_id()
                };

                uint16_t hitdelay = user.get_attackdelay(i) + wait;
                effect.hit_token = reserve_hit(oid, i, hitdelay);

                int16_t yoff = static_cast<int16_t>(
                    (static_cast<int32_t>(i) * 2 - static_cast<int32_t>(n) + 1)
                    * Constants::ARROW_SPREAD_Y / 2);
                Bullet hit_bullet { bullet_anim, origin + Point<int16_t>(0, yoff),
                                    result.toleft };

                bullet_effects_.emplace(hitdelay,
                                        std::move(effect),
                                        hit_bullet,
                                        body + Point<int16_t>(0, yoff));
                i++;
            }
        }

        if (result.damage_lines.empty()) {
            // An unaimed projectile flies to the far edge of its attack's strip, which apply_move
            // worked out from the weapon band (carrying the 远程箭 bonus) or the skill's own lt/rb.
            int16_t flight = result.shootdistance != 0 ? result.shootdistance : 400;
            int16_t xshift = result.toleft ? -flight : flight;
            Point<int16_t> target = origin + Point<int16_t>(xshift, -26);

            for (int i = 0; i < result.hitcount; i++) {
                DamageEffect effect { attack_user, {}, 0, false, 0, 0 };
                int16_t yoff = static_cast<int16_t>(
                    (i * 2 - result.hitcount + 1) * Constants::ARROW_SPREAD_Y / 2);
                Bullet hit_bullet { bullet_anim, origin + Point<int16_t>(0, yoff),
                                    result.toleft };
                bullet_effects_.emplace(user.get_attackdelay(i),
                                        std::move(effect),
                                        hit_bullet,
                                        target + Point<int16_t>(0, yoff));
            }
        }
    } else {
        for (const auto &[oid, wait] : order) {
            const auto &vec = result.damage_lines.at(oid);
            std::vector<DamageNumber> numbers = place_numbers(oid, vec);

            size_t i = 0;

            for (auto &number : numbers) {
                uint16_t hitdelay = user.get_attackdelay(i) + wait;
                uint64_t token = reserve_hit(oid, i, hitdelay);

                damage_effects_.emplace(hitdelay,
                                        attack_user,
                                        number,
                                        vec[i].first,
                                        result.toleft,
                                        oid,
                                        move.get_id(),
                                        result.knockback,
                                        token);

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