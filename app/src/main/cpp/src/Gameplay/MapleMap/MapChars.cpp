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
#include "MapChars.h"

#include <algorithm>

#include "OptionalCreator.h"

namespace ms {
void MapChars::draw(Layer::Id layer,
                    double viewx,
                    double viewy,
                    float alpha) const {
    chars_.draw(layer, viewx, viewy, alpha);
}

void MapChars::update(const Physics &physics) {
    for (; !spawns_.empty(); spawns_.pop_front()) {
        const auto &pending = spawns_.front();
        const CharSpawn &spawn = pending.spawn;
        const int32_t cid = spawn.get_cid();

        if (!get_char(cid)) {
            chars_.add(spawn.instantiate());
            auto &character = get_char(cid)->get();
            const auto &pets = spawn.get_pets();
            for (size_t index = 0; index < pets.size(); ++index) {
                add_pet(cid, static_cast<uint8_t>(index), pets[index]);
            }
            // Slot/UID checks must see every earlier add/remove, not the spawn snapshot.
            for (const auto &event : pending.pet_events) {
                apply_pet_event(character, event);
            }
        }
    }

    chars_.update(physics);
}

void MapChars::spawn(CharSpawn &&spawn) {
    const int32_t cid = spawn.get_cid();
    if (get_char(cid)) {
        return;
    }
    for (auto &pending : spawns_) {
        if (pending.spawn.get_cid() == cid) {
            // A replacement is a new snapshot; old events must not replay on it.
            pending.spawn = std::move(spawn);
            pending.pet_events.clear();
            return;
        }
    }
    spawns_.push_back({std::move(spawn), {}});
}

void MapChars::remove(int32_t cid) {
    spawns_.erase(std::remove_if(spawns_.begin(), spawns_.end(),
                                 [cid](const PendingSpawn &pending) {
                                     return pending.spawn.get_cid() == cid;
                                 }),
                   spawns_.end());
    chars_.remove(cid);
}

void MapChars::clear() {
    spawns_.clear();
    chars_.clear();
}

MapObjects *MapChars::get_chars() {
    return &chars_;
}

void MapChars::send_movement(int32_t cid,
                             const std::vector<Movement> &movements) {
    if (auto otherchar = get_char(cid)) {
        otherchar->get().send_movement(movements);
    }
}

void MapChars::add_pet(int32_t cid, uint8_t slot, const PetSpawn &pet) {
    if (pet.itemid <= 0 || !pet.uniqueid || pet.stance >= PetLook::Stance::LENGTH * 2) {
        return;
    }
    PetEvent event{PetEvent::Type::ADD, slot};
    event.pet = pet;
    send_pet_event(cid, std::move(event));
}

void MapChars::remove_pet(int32_t cid, uint8_t slot, bool hunger) {
    PetEvent event{PetEvent::Type::REMOVE, slot};
    event.hunger = hunger;
    send_pet_event(cid, std::move(event));
}

void MapChars::send_pet_movement(int32_t cid, uint8_t slot, int32_t uniqueid,
                                  const std::vector<Movement> &movements) {
    PetEvent event{PetEvent::Type::MOVEMENT, slot};
    event.uniqueid = uniqueid;
    event.movements = movements;
    send_pet_event(cid, std::move(event));
}

void MapChars::send_pet_event(int32_t cid, PetEvent event) {
    if (auto character = get_char(cid)) {
        apply_pet_event(character->get(), event);
        return;
    }
    for (auto &pending : spawns_) {
        if (pending.spawn.get_cid() == cid) {
            pending.pet_events.push_back(std::move(event));
            return;
        }
    }
    // No pending generation: do not retain this event for a later same-CID spawn.
}

void MapChars::apply_pet_event(OtherChar &character, const PetEvent &event) {
    auto &pets = character.get_pets();
    if (event.slot >= pets.size()) {
        return;
    }
    switch (event.type) {
        case PetEvent::Type::ADD: {
            const auto &pet = event.pet;
            character.add_pet(event.slot, pet.itemid, pet.name, pet.uniqueid,
                              pet.position, pet.stance, pet.fhid);
            pets[event.slot].set_remote();
            break;
        }
        case PetEvent::Type::REMOVE:
            character.remove_pet(event.slot, event.hunger);
            break;
        case PetEvent::Type::MOVEMENT:
            if (pets[event.slot].get_itemid() && event.uniqueid
                && pets[event.slot].get_uniqueid() == event.uniqueid) {
                // receive_movement also enforces the pet's remote_ flag.
                pets[event.slot].receive_movement(event.movements);
            }
            break;
    }
}

void MapChars::update_look(int32_t cid, const LookEntry &look) {
    if (auto otherchar = get_char(cid)) {
        otherchar->get().update_look(look);
    }
}

std::optional<std::reference_wrapper<OtherChar>> MapChars::get_char(
    int32_t cid) {
    return chars_.get<OtherChar>(cid);
}

std::optional<std::reference_wrapper<OtherChar>> MapChars::get_char(
    Point<int16_t> position,
    Point<int16_t> viewpos) {
    for (auto &chr : chars_) {
        if (inrange(chr.second->get_position(), position, viewpos)) {
            return create_optional<OtherChar>(chr.second.get());
        }
    }

    return {};
}

bool MapChars::inrange(Point<int16_t> char_pos,
                       Point<int16_t> cursorpos,
                       Point<int16_t> viewpos) const {
    Point<int16_t> absp = char_pos + viewpos;
    Point<int16_t> dim = Point<int16_t>();

    return Rectangle<int16_t>(absp.x() - dim.x() / 2 - 20,
                              absp.x() + dim.x() / 2 + 20,
                              absp.y() - dim.y() - 70,
                              absp.y())
        .contains(cursorpos);
}
}  // namespace ms