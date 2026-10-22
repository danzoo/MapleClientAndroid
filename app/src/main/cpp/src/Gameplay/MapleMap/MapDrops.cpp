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

#include <nlnx/node.hpp>
#include <nlnx/nx.hpp>

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
    nl::node src = nl::nx::item["Special"]["0900.img"];

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
    // per-item server delay or the frame the packet arrives on can shift its countdown by a tick
    // or two, so the items elapse across a few consecutive frames. Gating the clatter on a short
    // cooldown instead of "once per frame" collapses the whole burst into a single sound, while a
    // later, separate kill still clatters on its own.
    constexpr int16_t DROP_SOUND_COOLDOWN = 15;

    if (drop_sound_cooldown_ > 0) {
        --drop_sound_cooldown_;
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

        if (spawn.plays_sound() && drop_sound_cooldown_ == 0) {
            Sound(Sound::Name::DROP).play();
            drop_sound_cooldown_ = DROP_SOUND_COOLDOWN;
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
    if (auto drop = drops_.get<Drop>(oid)) {
        drop->get().expire(mode, looter);
    }
}

void MapDrops::clear() {
    drops_.clear();
}

MapDrops::Loot MapDrops::find_loot_at(Point<int16_t> playerpos) {
    if (!loot_enabled_) {
        return { 0, {} };
    }

    // Centre of the pile first: of the items under the character, take the one whose icon sits
    // closest to the character horizontally, then left before right, then earliest dropped.
    const std::vector<int32_t> oids = drops_.draw_order();

    int32_t best_oid = 0;
    Point<int16_t> best_pos;
    bool found = false;
    int64_t best_absdx = 0;
    int64_t best_dx = 0;

    for (auto iter = oids.begin(); iter != oids.end(); ++iter) {
        auto drop = drops_.get<const Drop>(*iter);

        if (!drop || !drop->get().is_active() || drop->get().claimed()) {
            continue;
        }

        const Rectangle<int16_t> box = drop->get().bounds();

        if (!box.contains(playerpos)) {
            continue;
        }

        const int64_t dx =
            (static_cast<int64_t>(box.left()) + static_cast<int64_t>(box.right())) / 2
            - static_cast<int64_t>(playerpos.x());
        const int64_t absdx = dx < 0 ? -dx : dx;

        if (!found || absdx < best_absdx || (absdx == best_absdx && dx < best_dx)) {
            found = true;
            best_oid = *iter;
            best_pos = drop->get().get_position();
            best_absdx = absdx;
            best_dx = dx;
        }
    }

    if (found) {
        loot_enabled_ = false;

        return { best_oid, best_pos };
    }

    return { 0, {} };
}

MapDrops::Loot MapDrops::find_loot_near(Point<int16_t> playerpos, int16_t radius) {
    if (!loot_enabled_) {
        return { 0, {} };
    }

    const int64_t rangesq = static_cast<int64_t>(radius) * radius;
    const std::vector<int32_t> oids = drops_.draw_order();

    // Of everything within reach, take the item closest to the character horizontally (centre of
    // the spread first), then left before right, then earliest dropped.
    int32_t best_oid = 0;
    Point<int16_t> best_pos;
    bool found = false;
    int64_t best_absdx = 0;
    int64_t best_dx = 0;

    for (auto iter = oids.begin(); iter != oids.end(); ++iter) {
        auto drop = drops_.get<const Drop>(*iter);

        if (!drop || !drop->get().is_active() || drop->get().claimed()) {
            continue;
        }

        // Measured against the icon as it is drawn, and to its nearest edge. The point the drop is
        // stored at sits inside that picture instead of at its middle, so a radius counted from it
        // reached further on one side of the character than on the other.
        const Rectangle<int16_t> box = drop->get().bounds();

        int64_t deltax = 0;

        if (playerpos.x() < box.left()) {
            deltax = box.left() - playerpos.x();
        } else if (playerpos.x() > box.right()) {
            deltax = playerpos.x() - box.right();
        }

        int64_t deltay = 0;

        if (playerpos.y() < box.top()) {
            deltay = box.top() - playerpos.y();
        } else if (playerpos.y() > box.bottom()) {
            deltay = playerpos.y() - box.bottom();
        }

        if (deltax * deltax + deltay * deltay > rangesq) {
            continue;
        }

        const int64_t dx =
            (static_cast<int64_t>(box.left()) + static_cast<int64_t>(box.right())) / 2
            - static_cast<int64_t>(playerpos.x());
        const int64_t absdx = dx < 0 ? -dx : dx;

        if (!found || absdx < best_absdx || (absdx == best_absdx && dx < best_dx)) {
            found = true;
            best_oid = *iter;
            best_pos = drop->get().get_position();
            best_absdx = absdx;
            best_dx = dx;
        }
    }

    if (found) {
        loot_enabled_ = false;

        return { best_oid, best_pos };
    }

    return { 0, {} };
}
}  // namespace ms