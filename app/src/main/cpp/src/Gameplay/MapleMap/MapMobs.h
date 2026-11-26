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

#include <queue>
#include <variant>

#include "../Combat/Attack.h"
#include "../Combat/SpecialMove.h"
#include "../Spawn.h"
#include "MapObjects.h"

namespace ms {
// A collection of mobs on a map.
class MapMobs {
public:
    // Draw all mobs on a layer.
    void draw(Layer::Id layer, double viewx, double viewy, float alpha) const;

    // Update all mobs.
    void update(const Physics &physics);

    // Spawn a new mob.
    void spawn(MobSpawn &&spawn);

    // Kill a mob.
    void remove(int32_t oid, int8_t effect);

    // Remove all mobs.
    void clear();

    // Queue a control revocation in the same order as spawns and full grants.
    void revoke_control(int32_t oid);

    // Update whether the server has a mob chasing the player.
    void set_aggro(int32_t oid, bool aggro);

    // Update a mob's hp display.
    void send_mobhp(int32_t oid, int8_t percent, uint16_t playerlevel);

    // Update a mob's movements.
    void send_movement(int32_t oid,
                       Point<int16_t> start,
                       std::vector<Movement> &&movements);

    // Calculate the results of an attack.
    void send_attack(AttackResult &result,
                     const Attack &attack,
                     const std::vector<int32_t> &targets,
                     uint8_t mobcount);

    // Applies damage to a mob.
    void apply_damage(int32_t oid,
                      int32_t damage,
                      bool toleft,
                      const AttackUser &user,
                      const SpecialMove &move,
                      uint8_t knockback = 0);

    // Record damage a character dealt to a mob, mirroring the server's drop-ownership rule.
    void note_damage(int32_t oid, int32_t cid, int32_t damage);

    // Whether cid dealt strictly more cumulative damage to this mob than anyone else. The server
    // hands a mob's drops to its highest damager, and in a party the drop packet's owner is the
    // party id, so this is the only way to tell one's own kills from a party member's.
    bool is_top_damager(int32_t oid, int32_t cid) const;

    // Register/retire one queued damage line, including projectiles stopped by terrain.
    void queue_hit(int32_t oid, uint64_t token, uint16_t delay);
    bool has_pending_hit(int32_t oid, uint64_t token) const;
    void finish_hit(int32_t oid, uint64_t token);

    // Extend the wait estimate while a registered projectile is still flying.
    void expect_hit(int32_t oid, uint16_t delay);

    // Estimated wait for the last locally queued blow, used to synchronize drops.
    int32_t pending_hit(int32_t oid) const;

    // Check if the mob with the specified oid exists.
    bool contains(int32_t oid) const;

    // Death waiting accepts local hits; DIE/fade/inactive/missing mobs do not.
    bool can_receive_hit(int32_t oid) const;

    // Return the id of the first mob who collides with the object.
    int32_t find_colliding(const MovingObject &moveobj) const;

    // Create an attack by the specified mob.
    MobAttack create_attack(int32_t oid) const;

    // Return the position of a mob.
    Point<int16_t> get_mob_position(int32_t oid) const;

    // Return the head position of a mob.
    Point<int16_t> get_mob_head_position(int32_t oid) const;

    // Return the middle of a mob's body, which is what a projectile is aimed at.
    Point<int16_t> get_mob_body_position(int32_t oid) const;

    // Return all mob map objects
    MapObjects *get_mobs();

private:
    // Apply preceding spawns/control changes before later movement or removal packets.
    void flush_spawn_events();

    struct ControlRevocation {
        int32_t oid;
    };

    MapObjects mobs_;
    std::queue<std::variant<MobSpawn, ControlRevocation>> spawn_events_;

    std::unordered_map<int32_t, std::unordered_map<int32_t, int64_t>> damage_by_mob_;
};
}  // namespace ms