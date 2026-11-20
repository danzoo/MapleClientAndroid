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

#include <array>
#include <chrono>
#include <queue>
#include <unordered_map>
#include <vector>

#include "../Graphics/Animation.h"
#include "../Spawn.h"
#include "MapObjects.h"

namespace ms {
class MapDrops {
public:
    MapDrops();

    // Initialize the meso icons.
    void init();

    // Draw all drops on a layer.
    void draw(Layer::Id layer, double viewx, double viewy, float alpha) const;

    // Update all drops.
    void update(const Physics &physics);

    // Spawn a new drop.
    void spawn(DropSpawn &&spawn);

    // Remove a drop.
    void remove(int32_t oid, int8_t mode, const PhysicsObject *looter);

    // Remove all drops, including delayed spawns.
    void clear();

    // Only an explicit server rejection identifies which item type should wait.
    void note_pickup_failure(int32_t oid, int32_t retry_after_ms);

    // Find the first eligible drop in draw order; return its OID and stable server destination.
    using Loot = std::pair<int32_t, Point<int16_t>>;

    Loot find_loot_at(Point<int16_t> playerpos);

    // Same geometry and priority; radius applies only to mesos (normally Constants::PICKUP_RANGE).
    Loot find_loot_near(Point<int16_t> playerpos, int16_t radius);

    // The next meso the meso-magnet buff may collect: one the player killed, popped out after the
    // buff was (re)applied, and inside the server's pickup authority box. Shares the
    // one-request-per-frame gate and the meso rejection cooldown with manual and pet pickup.
    Loot find_auto_meso(Point<int16_t> playerpos);

    // Marks the moment the buff became active; coins spawned before it are never magnet-collected.
    void note_magnet_start();

    // Filter pet equipment and ignored types before registering a request. INT32_MAX means mesos.
    Loot find_pet_loot(Point<int16_t> petpos, bool can_meso, bool can_item,
                       const std::vector<int32_t> &excluded);

private:
    using PickupClock = std::chrono::steady_clock;
    using PickupTime = PickupClock::time_point;

    // All looters share geometry, retry/type cooldowns, priority and the one-request-per-frame gate.
    Loot find_loot(Point<int16_t> looterpos, int16_t radius, bool can_meso, bool can_item,
                   const std::vector<int32_t> &excluded);

    // A server rejection holds back every drop of that item type (mesos are one type, key 0),
    // not just the one instance the request was sent for; other types stay pickable.
    bool type_pickup_ready(int32_t itemid, PickupTime now) const;

    std::unordered_map<int32_t, PickupTime> pickup_reject_at_;

    // Coins that appeared before this moment predate the current buff application.
    PickupTime magnet_since_{};

    MapObjects drops_;

    enum MesoIcon { BRONZE, GOLD, BUNDLE, BAG, NUM_ICONS };

    std::array<Animation, MesoIcon::NUM_ICONS> meso_icons_;
    bool loot_enabled_;

    // Counts down the window during which a further drop clatter is suppressed, so one mob's loot
    // burst - whose items can elapse a few ticks apart - sounds once instead of once per item.
    int16_t drop_sound_cooldown_;

    std::queue<DropSpawn> spawns_;
};
}  // namespace ms