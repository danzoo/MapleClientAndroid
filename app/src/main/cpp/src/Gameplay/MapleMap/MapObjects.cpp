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
#include "MapObjects.h"

#include <algorithm>

namespace ms {
namespace {
void erase_oid(std::vector<int32_t> &layer, int32_t oid) {
    layer.erase(std::remove(layer.begin(), layer.end(), oid), layer.end());
}
}  // namespace

void MapObjects::draw(Layer::Id layer,
                      double viewx,
                      double viewy,
                      float alpha) const {
    for (const auto &oid : layers_[layer]) {
        auto mmo = get<MapObject>(oid);

        if (mmo && mmo->get().is_active()) {
            mmo->get().draw(viewx, viewy, alpha);
        }
    }
}

void MapObjects::update(const Physics &physics) {
    for (auto iter = objects_.begin(); iter != objects_.end();) {
        bool remove_mob = false;

        if (auto &mmo = iter->second) {
            int8_t oldlayer = mmo->get_layer();
            int8_t newlayer = mmo->update(physics);

            if (newlayer == -1) {
                remove_mob = true;
            } else if (newlayer != oldlayer) {
                int32_t oid = iter->first;
                erase_oid(layers_[oldlayer], oid);
                layers_[newlayer].push_back(oid);
            }
        } else {
            remove_mob = true;
        }

        if (remove_mob) {
            iter = objects_.erase(iter);
        } else {
            iter++;
        }
    }
}

void MapObjects::clear() {
    objects_.clear();

    for (auto &layer : layers_) {
        layer.clear();
    }
}

bool MapObjects::contains(int32_t oid) const {
    return objects_.count(oid) > 0;
}

void MapObjects::add(std::unique_ptr<MapObject> toadd) {
    int32_t oid = toadd->get_oid();
    int8_t layer = toadd->get_layer();

    // A respawned oid can still be listed on the layer it was on before it expired. Drop the stale
    // entry so the object is drawn once, on its new layer, and at the end of that layer.
    for (auto &oids : layers_) {
        erase_oid(oids, oid);
    }

    objects_[oid] = std::move(toadd);
    layers_[layer].push_back(oid);
}

void MapObjects::remove(int32_t oid) {
    auto iter = objects_.find(oid);

    if (iter != objects_.end() && iter->second) {
        int8_t layer = iter->second->get_layer();
        objects_.erase(iter);

        erase_oid(layers_[layer], oid);
    }
}

// std::optional<std::reference_wrapper<MapObject>> MapObjects::get(int32_t oid)
// {
//     auto iter = objects_.find(oid);

//     if (iter != objects_.end()) {
//         return create_optional<MapObject>(iter->second.get());
//     }

//     return {};
// }

// std::optional<std::reference_wrapper<const MapObject>> MapObjects::get(
//     int32_t oid) const {
//     auto iter = objects_.find(oid);

//     if (iter != objects_.end()) {
//         return create_optional<const MapObject>(iter->second.get());
//     }

//     return {};
// }

MapObjects::underlying_t::iterator MapObjects::begin() {
    return objects_.begin();
}

MapObjects::underlying_t::iterator MapObjects::end() {
    return objects_.end();
}

MapObjects::underlying_t::const_iterator MapObjects::begin() const {
    return objects_.begin();
}

std::vector<int32_t> MapObjects::draw_order() const {
    std::vector<int32_t> oids;

    for (const auto &layer : layers_) {
        oids.insert(oids.end(), layer.begin(), layer.end());
    }

    return oids;
}

MapObjects::underlying_t::const_iterator MapObjects::end() const {
    return objects_.end();
}

MapObjects::underlying_t::size_type MapObjects::size() const {
    return objects_.size();
}
}  // namespace ms