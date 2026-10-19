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
#include "Mob.h"

#include <nlnx/nx.hpp>

#include "../../Net/Packets/GameplayPackets.h"
#include "../Stage.h"
#include "StringHandling.h"

namespace ms {
namespace {
auto fn_move_mob = []<typename... T>(T && ...args) {
    MoveMobPacket(std::forward<T>(args)...).dispatch();
};

// How near the player can get before a chasing mob stops bothering to re-face them. Measured
// against nothing, a mob standing on the player spins in place between two pixels.
constexpr double CHASE_TURN = 4.0;
}  // namespace

Mob::Mob(int32_t oi,
         int32_t mid,
         int8_t mode,
         int8_t st,
         uint16_t fh,
         bool newspawn,
         int8_t tm,
         Point<int16_t> position) :
    MapObject(oi) {
    std::string strid = string_format::extend_id(mid, 7);
    nl::node src = nl::nx::mob[strid + ".img"];

    nl::node info = src["info"];

    level_ = info["level"];
    watk_ = info["PADamage"];
    matk_ = info["MADamage"];
    wdef_ = info["PDDamage"];
    mdef_ = info["MDDamage"];
    accuracy_ = info["acc"];
    avoid_ = info["eva"];
    knockback_ = info["pushed"];
    speed_ = info["speed"];
    fly_speed_ = info["flySpeed"];
    touch_damage_ = info["bodyAttack"].get_bool();
    undead_ = info["undead"].get_bool();
    is_boss_ = info["boss"].get_bool();
    no_flip_ = info["noFlip"].get_bool();
    not_attack_ = info["notAttack"].get_bool();
    can_jump_ = src["jump"].size() > 0;
    can_fly_ = src["fly"].size() > 0;
    can_hit_ = src["hit1"].size() > 0;
    can_move_ = src["move"].size() > 0 || can_fly_;

    if (can_fly_) {
        animations_[Stance::STAND] = src["fly"];
        animations_[Stance::MOVE] = src["fly"];
    } else {
        animations_[Stance::STAND] = src["stand"];
        animations_[Stance::MOVE] = src["move"];
    }

    animations_[Stance::JUMP] = src["jump"];
    animations_[Stance::HIT] = src["hit1"];
    animations_[Stance::DIE] = src["die1"];

    name_ = std::string(nl::nx::string["Mob.img"][std::to_string(mid)]["name"]);

    nl::node sndsrc = nl::nx::sound["Mob.img"][strid];

    hit_sound_ = sndsrc["Damage"];
    die_sound_ = sndsrc["Die"];

    for (size_t i = 0; i < info["skill"].size(); i++) {
        auto skill_id = info["skill"][i]["skill"].get_integer();
        auto action = info["skill"][i]["action"].get_integer();
        skill_stands_.insert(
            { skill_id, src["skill" + std::to_string(action)] });
    }

    for (size_t i = 1; src["attack" + std::to_string(i)].size() > 0; i++) {
        attack_stands_.insert({ i, src["attack" + std::to_string(i)] });
    }

    // init or risk crash
    animations_[Stance::SKILL] = animations_[Stance::STAND];

    speed_ += 100;
    speed_ *= 0.001f;

    fly_speed_ += 100;
    fly_speed_ *= 0.0005f;

    if (can_fly_) {
        phobj_.type = PhysicsObject::Type::FLYING;
    }

    id_ = mid;
    team_ = tm;
    set_position(position);
    set_control(mode);
    phobj_.fhid = fh;
    phobj_.set_flag(PhysicsObject::Flag::TURN_AT_EDGES);

    hp_percent_ = 0;
    dying_ = false;
    dead_ = false;
    fading_ = false;
    death_pending_ = false;
    hit_played_ = false;
    death_wait_ = 0;
    pending_hit_ = 0;
    set_stance(st);
    fly_direction_ = STRAIGHT;
    counter_ = 0;

    name_label_ = Text(Text::Font::A13M,
                       Text::Alignment::CENTER,
                       Color::Name::WHITE,
                       Text::Background::NAMETAG,
                       name_);

    if (newspawn) {
        fade_in_ = true;
        opacity_.set(0.0f);
    } else {
        fade_in_ = false;
        opacity_.set(1.0f);
    }

    if (control_ && stance_ == Stance::STAND) {
        next_move();
    }
}

void Mob::set_stance(uint8_t stancebyte) {
    flip_ = (stancebyte % 2) == 0;

    if (!flip_) {
        stancebyte -= 1;
    }

    if (stancebyte < Stance::MOVE) {
        stancebyte = Stance::MOVE;
    }

    set_stance(static_cast<Stance>(stancebyte));
}

void Mob::set_stance(Stance newstance) {
    if (stance_ != newstance) {
        stance_ = newstance;

        animations_.at(stance_).reset();
    }
}

int8_t Mob::update(const Physics &physics) {
    if (!active_) {
        return phobj_.fhlayer;
    }

    bool aniend = animations_.at(stance_).update();

    if (aniend && stance_ == Stance::SKILL) {
        set_stance(Stance::STAND);
    }

    if (aniend && stance_ == Stance::DIE) {
        dead_ = true;
    }

    if (pending_hit_ > 0) {
        --pending_hit_;
    }

    if (death_pending_) {
        if (stance_ == Stance::HIT) {
            if (aniend || ++death_wait_ >= Constants::HIT_DEATH_WAIT) {
                death_pending_ = false;
                apply_death();
            }
        } else if (pending_hit_ == 0) {
            if (hit_played_) {
                // The blow this died of has already been reacted to; the kill simply turned up after
                // that flinch had played out. Reacting again would sound like two blows landing for
                // one swing, so the death goes straight ahead.
                death_pending_ = false;
                apply_death();
            } else {
                // Nothing local is going to land on this mob, so there is no blow to line the
                // flinch up with. Making one here keeps a death the client knows nothing about from
                // looking like the mob simply vanished.
                set_stance(Stance::HIT);
                hit_sound_.play();
                hit_played_ = true;
            }
        }
    }

    if (fading_) {
        opacity_ -= 0.025f;

        if (opacity_.last() < 0.025f) {
            opacity_.set(0.0f);
            fading_ = false;
            dead_ = true;
        }
    } else if (fade_in_) {
        opacity_ += 0.025f;

        if (opacity_.last() > 0.975f) {
            opacity_.set(1.0f);
            fade_in_ = false;
        }
    }

    if (dead_) {
        deactivate();

        return -1;
    }

    effects_.update();
    show_hp_.update();

    // remove later
    if (!buffs_.empty()) {
        effects_.add(buffs_.at(0).anim,
                     DrawArgument(get_head_position(Point<int16_t>()), false));
    }

    if (!dying_) {
        if (!can_fly_) {
            if (aggro_) {
                // Off after the player, so the end of the platform is not a barrier. The turn has
                // to be switched off whole: left armed it stops the mob at the one place the hate
                // is telling it to walk over. Walls are a separate check and still hold.
                phobj_.clear_flag(PhysicsObject::Flag::TURN_AT_EDGES);
            } else if (phobj_.is_flag_not_set(PhysicsObject::Flag::TURN_AT_EDGES)) {
                flip_ = !flip_;
                phobj_.set_flag(PhysicsObject::Flag::TURN_AT_EDGES);

                if (stance_ == Stance::HIT) {
                    set_stance(Stance::STAND);
                }
            }
        }

        // Chasing settles the facing here rather than in next_move: the wander only picks a
        // direction every couple of seconds, which is nowhere near often enough to follow someone
        // walking past. Blows and skills still play out, they simply lose the say over where the
        // mob goes the moment they are over.
        if (aggro_ && can_move_ && stance_ != Stance::HIT
            && stance_ != Stance::SKILL) {
            const double deltax =
                Stage::get().get_player().get_position().x() - phobj_.crnt_x();

            if (deltax > CHASE_TURN || deltax < -CHASE_TURN) {
                flip_ = deltax > 0.0;
            }

            if (stance_ == Stance::STAND) {
                set_stance(Stance::MOVE);
            }
        }

        switch (stance_) {
            case Stance::MOVE:
                if (can_fly_) {
                    phobj_.hforce = flip_ ? fly_speed_ : -fly_speed_;

                    switch (fly_direction_) {
                        case FlyDirection::UPWARDS:
                            phobj_.vforce = -fly_speed_;
                            break;
                        case FlyDirection::DOWNWARDS:
                            phobj_.vforce = fly_speed_;
                            break;
                    }
                } else {
                    phobj_.hforce = flip_ ? speed_ : -speed_;
                }

                break;
            case Stance::HIT:
                if (can_move_) {
                    double KBFORCE = phobj_.onground ? 0.2 : 0.1;
                    phobj_.hforce = flip_ ? -KBFORCE : KBFORCE;

                    // The platform lookup follows the chain of footholds off the end of a
                    // platform, so a mob shoved as far as the edge of the one it stands on has to
                    // be stopped there rather than knocked off it.
                    if (phobj_.onground && phobj_.fhid) {
                        const Physics::Platform platform =
                            physics.get_platform(phobj_.fhid);
                        const double projected =
                            phobj_.crnt_x() + phobj_.hspeed + phobj_.hforce;
                        const double edgeleft =
                            platform.left + Constants::PLATFORM_EDGE_MARGIN;
                        const double edgeright =
                            platform.right - Constants::PLATFORM_EDGE_MARGIN;

                        if (projected > edgeright || projected < edgeleft) {
                            phobj_.hforce = 0.0;
                            phobj_.hspeed = 0.0;
                        }
                    }
                }

                break;
            case Stance::JUMP: phobj_.vforce = -5.0; break;
        }

        // A mob that wanders stays on the platform it is standing on. Off the end of it the
        // foothold lookup hands the mob to whatever is below, so without this a slow walk to the
        // edge becomes a drop it cannot climb back out of. Being after the player is the one
        // reason to step over that edge; walls still stop it either way.
        if (!aggro_ && !can_fly_ && !can_jump_ && phobj_.onground && phobj_.fhid) {
            const Physics::Platform platform = physics.get_platform(phobj_.fhid);
            const double edgeleft =
                platform.left + Constants::PLATFORM_EDGE_MARGIN;
            const double edgeright =
                platform.right - Constants::PLATFORM_EDGE_MARGIN;
            const double x = phobj_.crnt_x();

            // Held to the stretch only while standing on it, so a mob that has come off somewhere
            // else keeps walking instead of freezing where the two margins cross over.
            if (edgeleft < edgeright && x >= platform.left && x <= platform.right) {
                const double projected = x + phobj_.hspeed + phobj_.hforce;

                if (projected > edgeright || projected < edgeleft) {
                    phobj_.hforce = 0.0;
                    phobj_.hspeed = 0.0;

                    // Turning is left to the edge logic above, which flips the mob on the tick it
                    // finds this flag gone. Flipping here as well would cancel that one out.
                    phobj_.clear_flag(PhysicsObject::Flag::TURN_AT_EDGES);
                }
            }
        }

        physics.move_object(phobj_);

        if (control_) {
            counter_++;

            bool next = false;

            switch (stance_) {
                case Stance::HIT: next = counter_ > 200; break;
                case Stance::JUMP: next = phobj_.onground; break;
                default: next = aniend && counter_ > 200; break;
            }

            if (next) {
                next_move();
                update_movement();
                counter_ = 0;
            }
        }
    } else {
        phobj_.normalize();
        physics.get_fht().update_fh(phobj_);
    }

    return phobj_.fhlayer;
}

void Mob::next_move() {
    if (can_move_) {
        switch (stance_) {
            case Stance::HIT:
            case Stance::STAND:
                set_stance(Stance::MOVE);
                flip_ = randomizer_.next_bool();
                break;
            case Stance::MOVE:
            case Stance::JUMP:
                if (can_jump_ && phobj_.onground && randomizer_.below(0.25f)) {
                    set_stance(Stance::JUMP);
                } else {
                    switch (randomizer_.next_int(3)) {
                        case 0: set_stance(Stance::STAND); break;
                        case 1:
                            set_stance(Stance::MOVE);
                            flip_ = false;
                            break;
                        case 2:
                            set_stance(Stance::MOVE);
                            flip_ = true;
                            break;
                    }
                }

                break;
        }

        if (stance_ == Stance::MOVE && can_fly_) {
            fly_direction_ =
                randomizer_.next_enum(FlyDirection::NUM_DIRECTIONS);
        }
    } else {
        set_stance(Stance::STAND);
    }
}

void Mob::update_movement() {
    fn_move_mob(oid_,
                1,
                0,
                0,
                0,
                0,
                0,
                get_position(),
                Movement(phobj_, value_of(stance_, flip_)));
}

void Mob::update_movement(int16_t type,
                          int8_t nibbles,
                          int8_t action,
                          int8_t skill,
                          int8_t skill_level,
                          int16_t option) {
    fn_move_mob(oid_,
                type,
                nibbles,
                action,
                skill,
                skill_level,
                option,
                get_position(),
                Movement(phobj_, value_of(stance_, flip_)));
}

void Mob::give_buff(const MobBuff &buff) {
    buffs_.push_back(buff);
}

void Mob::use_skill(const MobSkill &skill) {
    animations_[Stance::SKILL] = skill_stands_.at(skill.get_id());
    set_stance(Stance::SKILL);

    if (skill.is_buff() && buffs_.empty()) {
        buffs_.push_back(skill.get_buff());
    }
}

void Mob::use_attack(const MobSpecialAttack &attack) {
    animations_[Stance::SKILL] = attack_stands_.at(attack.get_id());
    set_stance(Stance::SKILL);
}

void Mob::use_some_attack() {
    if (stance_ == Stance::SKILL || attack_stands_.empty()) {
        return;
    }

    animations_[Stance::SKILL] =
        attack_stands_.at((std::rand() % attack_stands_.size()) + 1);
    set_stance(Stance::SKILL);
}

void Mob::cancel_buff(int32_t buff) {
    buffs_.clear();
}

bool Mob::has_buff() const {
    return !buffs_.empty();
}

void Mob::draw(double viewx, double viewy, float alpha) const {
    Point<int16_t> absp = phobj_.get_absolute(viewx, viewy, alpha);
    Point<int16_t> headpos = get_head_position(absp);

    effects_.drawbelow(absp, alpha);

    if (!dead_) {
        float interopc = opacity_.get(alpha);

        animations_.at(stance_).draw(
            DrawArgument(absp, flip_ && !no_flip_, interopc),
            alpha);

        if (show_hp_) {
            name_label_.draw(absp);

            // The bar stays up while the death is still waiting for the blow that caused it.
            if ((!dying_ || death_pending_) && hp_percent_ > 0) {
                hp_bar_.draw(headpos, hp_percent_);
            }
        }
    }

    effects_.drawabove(absp, alpha);
}

void Mob::set_control(int8_t mode) {
    control_ = mode > 0;
    set_aggro(mode == 2);
}

void Mob::set_aggro(bool aggro) {
    if (aggro_ && !aggro && !can_fly_) {
        // Coming off a chase with the edge turn switched off, so put it back before the update loop
        // sees the flag down and reads it as something the mob has just walked into.
        phobj_.set_flag(PhysicsObject::Flag::TURN_AT_EDGES);
    }

    aggro_ = aggro;
}

void Mob::send_movement(Point<int16_t> start,
                        std::vector<Movement> &&in_movements) {
    if (control_) {
        return;
    }

    set_position(start);

    movements_ = std::forward<decltype(in_movements)>(in_movements);

    if (movements_.empty()) {
        return;
    }

    const Movement &lastmove = movements_.front();

    uint8_t laststance = lastmove.newstate;
    set_stance(laststance);

    phobj_.fhid = lastmove.fh;
}

Point<int16_t> Mob::get_head_position(Point<int16_t> position) const {
    Point<int16_t> head = animations_.at(stance_).get_head();

    position.shift_x((flip_ && !no_flip_) ? -head.x() : head.x());
    position.shift_y(head.y());

    return position;
}

void Mob::kill(int8_t animation) {
    switch (animation) {
        case 0: deactivate(); break;
        case 1:
            dying_ = true;

            // The server has already resolved the blow the player is still swinging: the damage
            // that killed this mob is sitting in the local attack queue and lands a few hundred
            // milliseconds from now. Waiting for it is what keeps the flinch, the death and the
            // loot in step with the attack instead of running ahead of it.
            if (can_hit_ && stance_ != Stance::DIE) {
                death_pending_ = true;
                death_wait_ = 0;

                // Movement stops the moment the mob is marked dying, so a mob that was walking has
                // to stop moving its legs too or it slides through the wait.
                if (stance_ == Stance::MOVE) {
                    set_stance(Stance::STAND);
                }
            } else {
                apply_death();
            }
            break;
        case 2:
            fading_ = true;
            dying_ = true;
            break;
    }
}

void Mob::show_hp(int8_t percent, uint16_t playerlevel) {
    if (hp_percent_ == 0) {
        int16_t delta = playerlevel - level_;

        if (delta > 9) {
            name_label_.change_color(Color::Name::YELLOW);
        } else if (delta < -9) {
            name_label_.change_color(Color::Name::RED);
        }
    }

    if (percent > 100) {
        percent = 100;
    } else if (percent < 0) {
        percent = 0;
    }

    hp_percent_ = percent;
    show_hp_.set_for(2000);
}

void Mob::show_effect(const Animation &animation,
                      int8_t pos,
                      int8_t z,
                      bool f) {
    if (!active_) {
        return;
    }

    Point<int16_t> shift;

    switch (pos) {
        case 0: shift = get_head_position(Point<int16_t>()); break;
        case 1: break;
        case 2: break;
        case 3: break;
        case 4: break;
    }

    effects_.add(animation, DrawArgument(shift, f), z);
}

float Mob::calculate_hitchance(int16_t leveldelta,
                               int32_t player_accuracy) const {
    float faccuracy = static_cast<float>(player_accuracy);
    float hitchance =
        faccuracy / (((1.84f + 0.07f * leveldelta) * avoid_) + 1.0f);

    if (hitchance < 0.01f) {
        hitchance = 0.01f;
    }

    return hitchance;
}

double Mob::calculate_mindamage(int16_t leveldelta,
                                double damage,
                                bool magic) const {
    double mindamage = magic ? damage - (1 + 0.01 * leveldelta) * mdef_ * 0.6
                             : damage * (1 - 0.01 * leveldelta) - wdef_ * 0.6;

    return mindamage < 1.0 ? 1.0 : mindamage;
}

double Mob::calculate_maxdamage(int16_t leveldelta,
                                double damage,
                                bool magic) const {
    double maxdamage = magic ? damage - (1 + 0.01 * leveldelta) * mdef_ * 0.5
                             : damage * (1 - 0.01 * leveldelta) - wdef_ * 0.5;

    return maxdamage < 1.0 ? 1.0 : maxdamage;
}

std::vector<std::pair<int32_t, bool>> Mob::calculate_damage(
    const Attack &attack) {
    double mindamage;
    double maxdamage;
    float hitchance;
    float critical;
    int16_t leveldelta = level_ - attack.playerlevel;

    if (leveldelta < 0) {
        leveldelta = 0;
    }

    Attack::DamageType damagetype = attack.damagetype;

    switch (damagetype) {
        case Attack::DamageType::DMG_WEAPON:
        case Attack::DamageType::DMG_MAGIC:
            mindamage = calculate_mindamage(
                leveldelta,
                attack.mindamage,
                damagetype == Attack::DamageType::DMG_MAGIC);
            maxdamage = calculate_maxdamage(
                leveldelta,
                attack.maxdamage,
                damagetype == Attack::DamageType::DMG_MAGIC);
            hitchance = calculate_hitchance(leveldelta, attack.accuracy);
            critical = attack.critical;
            break;
        case Attack::DamageType::DMG_FIXED:
            mindamage = attack.fixdamage;
            maxdamage = attack.fixdamage;
            hitchance = 1.0f;
            critical = 0.0f;
            break;
    }

    std::vector<std::pair<int32_t, bool>> result(attack.hitcount);

    std::generate(result.begin(), result.end(), [&]() {
        return next_damage(mindamage, maxdamage, hitchance, critical);
    });

    update_movement();

    return result;
}

std::pair<int32_t, bool> Mob::next_damage(double mindamage,
                                          double maxdamage,
                                          float hitchance,
                                          float critical) const {
    bool hit = randomizer_.below(hitchance);

    if (!hit) {
        return std::pair<int32_t, bool>(0, false);
    }

    constexpr double DAMAGECAP = 999999.0;

    double damage = randomizer_.next_real(mindamage, maxdamage);
    bool iscritical = randomizer_.below(critical);

    if (iscritical) {
        damage *= 1.5;
    }

    if (damage < 1) {
        damage = 1;
    } else if (damage > DAMAGECAP) {
        damage = DAMAGECAP;
    }

    auto intdamage = static_cast<int32_t>(damage);

    return std::pair<int32_t, bool>(intdamage, iscritical);
}

void Mob::apply_damage(int32_t damage, bool toleft) {
    if (death_pending_) {
        // This is the blow the server already reported the death of. It gets the flinch, and the
        // death animation follows once the flinch has played out.
        pending_hit_ = 0;

        if (stance_ != Stance::HIT) {
            flip_ = toleft;
            set_stance(Stance::HIT);
            hit_sound_.play();
            hit_played_ = true;
        }

        return;
    }

    hit_sound_.play();
    hit_played_ = true;

    if (dying_ && stance_ != Stance::DIE) {
        apply_death();
    } else if (control_ && is_alive() && damage >= knockback_ && !is_boss_) {
        flip_ = toleft;
        counter_ = 170;
        set_stance(Stance::HIT);

        update_movement();
    }
}

void Mob::expect_hit(int16_t ticks) {
    if (ticks > pending_hit_) {
        pending_hit_ = ticks;
        hit_played_ = false;
    }
}

int16_t Mob::pending_hit() const {
    return pending_hit_;
}

MobAttack Mob::create_touch_attack() const {
    if (!touch_damage_) {
        return MobAttack();
    }

    int32_t minattack = static_cast<int32_t>(watk_ * 0.8f);
    int32_t maxattack = watk_;
    int32_t attack = randomizer_.next_int(minattack, maxattack);

    return MobAttack(attack, get_position(), id_, oid_);
}

void Mob::apply_death() {
    set_stance(Stance::DIE);
    die_sound_.play();
    dying_ = true;
}

bool Mob::is_alive() const {
    return active_ && !dying_;
}

bool Mob::is_in_range(const Rectangle<int16_t> &range) const {
    if (!active_) {
        return false;
    }

    Rectangle<int16_t> bounds = animations_.at(stance_).get_bounds();
    bounds.shift(get_position());

    return range.overlaps(bounds);
}

Point<int16_t> Mob::get_head_position() const {
    Point<int16_t> position = get_position();

    return get_head_position(position);
}

const MobSkill &Mob::get_move(int32_t move_id, uint8_t level) {
    auto iter = skills_.find(move_id);

    if (iter == skills_.end()) {
        iter = skills_.emplace(move_id, MobSkill(move_id, level)).first;
    }

    return iter->second;
}

const MobSpecialAttack &Mob::get_move(int32_t move_id) {
    auto iter = attacks_.find(move_id);

    if (iter == attacks_.end()) {
        iter = attacks_.emplace(move_id, MobSpecialAttack(id_, move_id)).first;
    }

    return iter->second;
}
}  // namespace ms