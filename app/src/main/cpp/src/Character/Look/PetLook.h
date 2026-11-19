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

#include <optional>
#include <vector>

#include "../../Gameplay/Movement.h"
#include "../../Gameplay/Physics/Physics.h"
#include "../../Graphics/Animation.h"
#include "../../Graphics/Text.h"
#include "../../Template/EnumMap.h"

namespace ms {
class PetLook {
public:
    enum Stance : uint8_t {
        MOVE,
        STAND,
        JUMP,
        ALERT,
        PRONE,
        FLY,
        HANG,
        WARP,
        LENGTH
    };

    static Stance stancebyvalue(uint8_t value) {
        uint8_t valueh = value / 2;

        return valueh >= LENGTH ? STAND : static_cast<Stance>(valueh);
    }

    PetLook(int32_t iid,
            std::string name,
            int32_t uniqueid,
            Point<int16_t> pos,
            uint8_t stance,
            int32_t fhid);

    PetLook();

    void draw(double viewx, double viewy, float alpha) const;

    void update(const Physics &physics, Point<int16_t> charpos);

    // Remote pets animate locally but are positioned only by server movement.
    void set_remote();
    void receive_movement(const std::vector<Movement> &movements);

    void set_position(int16_t xpos, int16_t ypos);
    void set_foothold(uint16_t fhid) { phobj_.fhid = fhid; }

    void set_stance(Stance stance);

    void set_stance(uint8_t stancebyte);

    int32_t get_itemid() const;

    Stance get_stance() const;

    int32_t get_uniqueid() const;
    Point<int16_t> get_position() const;

    // Called only for the local player's pets, never from shared look updates.
    std::optional<Movement> take_movement(bool force);
    void reset_movement();

    void set_excluded_items(std::vector<int32_t> items);
    const std::vector<int32_t> &get_excluded_items() const;

private:
    std::optional<Movement> last_sent_move_;
    int32_t sync_elapsed_ = 0;
    std::vector<int32_t> excluded_items_;

    int32_t item_id_;
    std::string name_;
    int32_t unique_id_;
    Stance stance_ = Stance::STAND;
    bool flip_ = false;
    bool remote_ = false;

    EnumMap<Stance, Animation> animations_;
    PhysicsObject phobj_;
    Text name_label_;
};
}  // namespace ms