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

#include "../../Audio/Audio.h"
#include "../../Net/Login.h"
#include "../../Template/Interpolated.h"
#include "../../Template/Rectangle.h"
#include "../../Util/Randomizer.h"
#include "../../Util/TimedBool.h"
#include "Body.h"
#include "CharEquips.h"
#include "Face.h"
#include "Hair.h"

namespace ms {
class CharLook {
public:
    CharLook(const LookEntry &entry);

    CharLook();

    void reset();

    void draw(const DrawArgument &args, float alpha) const;

    void draw(Point<int16_t> pos,
              bool flip,
              Stance::Id stance,
              Expression::Id expression) const;

    bool update(uint16_t timestep);

    void set_hair(int32_t hairid);

    void set_body(int32_t skinid);

    void set_face(int32_t faceid);

    void add_equip(int32_t equipid);
    void remove_equip(EquipSlot::Id slot);

    void attack(bool degenerate);

    void attack(Stance::Id stance);

    // Arms the weapon sound queued by attack() to fire once the stance reaches this frame.
    void set_attack_sound_frame(uint8_t frame);

    void set_stance(Stance::Id stance);

    void set_expression(Expression::Id expression);

    void set_action(const std::string &action);

    void set_direction(bool mirrored);

    void set_alerted(int64_t millis);

    bool get_alerted() const;

    bool is_twohanded(Stance::Id stance) const;

    uint16_t get_attackdelay(size_t no, uint8_t first_frame) const;

    uint8_t get_frame() const;

    Stance::Id get_stance() const;

    // Offset from the physical character position to the geometric center of the current
    // body/head canvas bounds, including the action's draw shift but no hair or equipment.
    Point<int16_t> get_attack_origin_offset() const;

    const Body *get_body() const;

    const Hair *get_hair() const;

    const Face *get_face() const;

    const CharEquips &get_equips() const;

    // Close-range switching box, relative to the current attack origin and facing left.
    // Preserve the candidate intersection's dimensions, center it vertically, and extend
    // it forward from the origin. This is not a replacement for the IMG melee damage box.
    Rectangle<int16_t> get_degenerate_range() const;

    // Initialize drawinfo
    static void init();

private:
    void updatetwohanded();

    void draw(const DrawArgument &args,
              Stance::Id interstance,
              Expression::Id interexp,
              uint8_t interframe,
              uint8_t interfcframe) const;

    Point<int16_t> get_attack_origin_offset(Stance::Id stance,
                                           uint8_t frame,
                                           Point<int16_t> move,
                                           bool facing_right) const;

    uint16_t get_delay(Stance::Id stance, uint8_t frame) const;

    uint8_t getnextframe(Stance::Id stance, uint8_t frame);

    Stance::Id getattackstance(uint8_t attack, bool degenerate) const;

    Nominal<Stance::Id> stance_;
    Nominal<uint8_t> st_frame_;
    uint16_t st_elapsed_;
    // Direction the idle stand walks its frames in: it breathes back and forth (0,1,2,1,0,…)
    // rather than wrapping, so the step flips at each end of the run.
    int8_t stand_step_ = 1;

    Nominal<Expression::Id> expression_;
    Nominal<uint8_t> exp_frame_;
    uint16_t exp_elapsed_;

    bool flip_;

    const BodyAction *action_;
    std::string actionstr_;
    uint8_t actframe_;

    Sound attack_sound_;
    uint8_t attack_sound_frame_;
    bool attack_sound_pending_;

    const Body *body_;
    const Hair *hair_;
    const Face *face_;
    CharEquips equips_;

    Randomizer randomizer_;
    TimedBool alerted_;

    static BodyDrawInfo draw_info_;
    static std::unordered_map<int32_t, Hair> hair_styles_;
    static std::unordered_map<int32_t, Face> face_types_;
    static std::unordered_map<int32_t, Body> body_types_;
};
}  // namespace ms