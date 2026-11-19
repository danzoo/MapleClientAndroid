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

#include <deque>
#include <vector>

#include "../Character/OtherChar.h"
#include "../Movement.h"
#include "../Spawn.h"
#include "MapObjects.h"

namespace ms {
// A collection of remote controlled characters on a map.
class MapChars {
public:
    // Draw all characters on a layer.
    void draw(Layer::Id layer, double viewx, double viewy, float alpha) const;

    // Update all characters.
    void update(const Physics &physics);

    // Spawn a new character, if it has not been spawned yet.
    void spawn(CharSpawn &&spawn);

    // Remove a character.
    void remove(int32_t cid);

    // Remove all characters.
    void clear();

    // Returns a reference to the MapObjects object.
    MapObjects *get_chars();

    // Update a characters movement.
    void send_movement(int32_t cid, const std::vector<Movement> &movements);

    // Apply remote pet events now, or in packet order after a pending spawn.
    // Events without a character or pending spawn are ignored.
    void add_pet(int32_t cid, uint8_t slot, const PetSpawn &pet);
    void remove_pet(int32_t cid, uint8_t slot, bool hunger);
    void send_pet_movement(int32_t cid, uint8_t slot, int32_t uniqueid,
                           const std::vector<Movement> &movements);

    // Update a characters look.
    void update_look(int32_t cid, const LookEntry &look);

    std::optional<std::reference_wrapper<OtherChar>> get_char(int32_t cid);

    std::optional<std::reference_wrapper<OtherChar>> get_char(
        Point<int16_t> position,
        Point<int16_t> viewpos);

    bool inrange(Point<int16_t> char_pos,
                 Point<int16_t> cursorpos,
                 Point<int16_t> viewpos) const;

private:
    struct PetEvent {
        enum class Type { ADD, REMOVE, MOVEMENT };
        Type type;
        uint8_t slot;
        PetSpawn pet{};
        bool hunger = false;
        int32_t uniqueid = 0;
        std::vector<Movement> movements{};
    };

    struct PendingSpawn {
        CharSpawn spawn;
        std::vector<PetEvent> pet_events;
    };

    void send_pet_event(int32_t cid, PetEvent event);
    static void apply_pet_event(OtherChar &character, const PetEvent &event);

    MapObjects chars_;

    // Events belong to this exact snapshot, never to a future use of its CID.
    std::deque<PendingSpawn> spawns_;
};
}  // namespace ms