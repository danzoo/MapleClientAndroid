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
#include "MapDrops.h"

#include <algorithm>
#include <limits>

#include <imgwz/node.hpp>
#include <imgwz/wz.hpp>

#include "../Audio/Audio.h"
#include "../Constants.h"
#include "../Data/ItemData.h"
#include "Drop.h"
#include "ItemDrop.h"
#include "MesoDrop.h"
#include "OptionalCreator.h"

namespace ms {
MapDrops::MapDrops() : loot_enabled_(false), drop_sound_cooldown_(0) {}

void MapDrops::init() {
    img::node src = img::wz::item["Special"]["0900.img"];

    meso_icons_[MesoIcon::BRONZE] = src["09000000"]["iconRaw"];
    meso_icons_[MesoIcon::GOLD] = src["09000001"]["iconRaw"];
    meso_icons_[MesoIcon::BUNDLE] = src["09000002"]["iconRaw"];
    meso_icons_[MesoIcon::BAG] = src["09000003"]["iconRaw"];
}

void MapDrops::draw(Layer::Id layer,
                    double viewx,
                    double viewy,
                    float alpha) const {
    drops_.draw(layer, viewx, viewy, alpha);
}

void MapDrops::update(const Physics &physics) {
    // A mob's loot burst does not always land on one frame: each item is its own packet, and the
    // per-item server delay or the frame the packet arrives on can shift its countdown by an update
    // or two, so the items elapse across a few consecutive frames. Gating the clatter on a short
    // cooldown instead of "once per frame" collapses the whole burst into a single sound, while a
    // later, separate kill still clatters on its own. The cooldown is
    // Constants::DROP_SOUND_COOLDOWN_MS.

    if (drop_sound_cooldown_ > 0) {
        drop_sound_cooldown_ -= Constants::TIMESTEP;
    }

    for (size_t pending = spawns_.size(); pending > 0; --pending) {
        DropSpawn spawn = std::move(spawns_.front());

        spawns_.pop();

        if (!spawn.elapsed()) {
            // The mob it came out of is still dying. Put it back so that a drop with a shorter
            // wait, or none at all, isn't held up behind this one.
            spawns_.emplace(std::move(spawn));
            continue;
        }

        if (spawn.plays_sound() && drop_sound_cooldown_ <= 0) {
            Sound(Sound::Name::DROP).play();
            drop_sound_cooldown_ = Constants::DROP_SOUND_COOLDOWN_MS;
        }

        int32_t oid = spawn.get_oid();

        if (auto drop = drops_.get<MapObject>(oid)) {
            drop->get().makeactive();
        } else {
            int32_t itemid = spawn.get_itemid();
            bool meso = spawn.is_meso();

            if (meso) {
                MesoIcon mesotype = (itemid > 999)
                                        ? BAG
                                        : (itemid > 99)
                                              ? BUNDLE
                                              : (itemid > 49) ? GOLD : BRONZE;

                const Animation &icon = meso_icons_[mesotype];
                drops_.add(spawn.instantiate(icon));
            } else if (const ItemData &itemdata = ItemData::get(itemid)) {
                const Texture &icon = itemdata.get_icon(true);
                drops_.add(spawn.instantiate(icon));
            }
        }
    }

    for (auto &mesoicon : meso_icons_) {
        mesoicon.update();
    }

    drops_.update(physics);

    loot_enabled_ = true;
}

void MapDrops::spawn(DropSpawn &&spawn) {
    spawns_.emplace(std::move(spawn));
}

void MapDrops::remove(int32_t oid, int8_t mode, const PhysicsObject *looter) {
    // Another player can collect it while its local death animation still delays the spawn.
    // A final removal must cancel that queue entry too, or it later reappears as phantom loot.
    for (size_t pending = spawns_.size(); pending > 0; --pending) {
        DropSpawn spawn = std::move(spawns_.front());
        spawns_.pop();
        if (spawn.get_oid() != oid) {
            spawns_.emplace(std::move(spawn));
        }
    }
    if (auto drop = drops_.get<Drop>(oid)) {
        drop->get().expire(mode, looter);
    }
}

void MapDrops::clear() {
    drops_.clear();
    spawns_ = {};
    loot_enabled_ = false;
    drop_sound_cooldown_ = 0;
    pickup_reject_at_.clear();
}

bool MapDrops::type_pickup_ready(int32_t itemid, PickupTime now) const {
    auto retry = pickup_reject_at_.find(itemid);

    return retry == pickup_reject_at_.end() || now >= retry->second;
}

void MapDrops::note_pickup_failure(int32_t oid, int32_t retry_after_ms) {
    auto drop = drops_.get<Drop>(oid);

    if (!drop || !drop->get().consume_pickup_reply()) {
        return;
    }

    pickup_reject_at_[drop->get().itemid()] =
        PickupClock::now() + std::chrono::milliseconds(std::clamp(retry_after_ms, 100, 5000));
}

MapDrops::Loot MapDrops::find_loot_at(Point<int16_t> playerpos) {
    return find_loot(playerpos, Constants::PICKUP_RANGE, true, true, {});
}

MapDrops::Loot MapDrops::find_loot_near(Point<int16_t> playerpos, int16_t radius) {
    return find_loot(playerpos, radius, true, true, {});
}

MapDrops::Loot MapDrops::find_pet_loot(Point<int16_t> petpos, bool can_meso, bool can_item,
                                     const std::vector<int32_t> &excluded) {
    return find_loot(petpos, Constants::PICKUP_RANGE, can_meso, can_item, excluded);
}

MapDrops::Loot MapDrops::find_loot(Point<int16_t> looterpos, int16_t radius,
                                 bool can_meso, bool can_item,
                                 const std::vector<int32_t> &excluded) {
    if (!loot_enabled_) {
        return { 0, {} };
    }

    const PickupTime now = PickupClock::now();

    // Earliest dropped first: draw_order() is the order drops were added to their layer, so the
    // bottom of a stack is the first one registered and the one the hand closes on. This matches
    // the original PC client; do not rank candidates by their distance to the character.
    const std::vector<int32_t> oids = drops_.draw_order();

    for (auto iter = oids.begin(); iter != oids.end(); ++iter) {
        auto drop = drops_.get<const Drop>(*iter);

        if (!drop) {
            continue;
        }

        const int32_t itemid = drop->get().itemid();
        const bool meso = itemid == 0;
        const int32_t excluded_id = meso ? std::numeric_limits<int32_t>::max() : itemid;

        // A filtered pet candidate must not consume the frame gate or start its pickup retry.
        if ((meso ? !can_meso : !can_item)
            || std::find(excluded.begin(), excluded.end(), excluded_id) != excluded.end()) {
            continue;
        }

        if (!drop->get().can_request_pickup(now) || !type_pickup_ready(itemid, now)
            || !drop->get().within_pickup_range(looterpos, radius)) {
            continue;
        }

        loot_enabled_ = false;
        drops_.get<Drop>(*iter)->get().note_pickup_request(now);

        return { *iter, drop->get().get_pickup_position() };
    }

    return { 0, {} };
}
}  // namespace ms