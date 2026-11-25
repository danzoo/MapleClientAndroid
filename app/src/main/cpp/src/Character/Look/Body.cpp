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
#include "Body.h"

#include <algorithm>
#include <array>
#include <imgwz/wz.hpp>
#include <string>

#include "StringHandling.h"

namespace ms {
Body::Body(int32_t skin, const BodyDrawInfo &drawinfo) {
    std::string strid = string_format::extend_id(skin, 2);
    img::node bodynode = img::wz::character["000020" + strid + ".img"];
    img::node headnode = img::wz::character["000120" + strid + ".img"];

    for (const auto &[stance, stance_name] : Stance::names) {
        img::node stancenode = bodynode[stance_name];

        if (!stancenode) {
            continue;
        }

        for (int frame = 0; img::node framenode = stancenode[frame]; ++frame) {
            for (const auto &partnode : framenode) {
                std::string part = partnode.name();

                if (part != "delay" && part != "face") {
                    std::string z = partnode["z"];
                    Body::Layer layer = layer_by_name(z);

                    if (layer == Body::Layer::NONE) {
                        continue;
                    }

                    Point<int16_t> shift;

                    switch (layer) {
                        case Body::Layer::HAND_BELOW_WEAPON:
                            shift = drawinfo.get_hand_position(stance, frame);
                            shift -= partnode["map"]["handMove"];
                            break;
                        default:
                            shift = drawinfo.get_body_position(stance, frame);
                            if (Stance::Id::DEAD == stance) {
                                shift -= Point<int16_t>(5, 4);
                            }

                            shift -= partnode["map"]["navel"];
                            break;
                    }

                    stances_[stance][layer]
                        .emplace(frame, partnode)
                        .first->second.shift(shift);
                }
            }

            if (img::node headsfnode = headnode[stance_name][frame]["head"]) {
                Point<int16_t> shift =
                    drawinfo.get_head_position(stance, frame);

                stances_[stance][Body::Layer::HEAD]
                    .emplace(frame, headsfnode)
                    .first->second.shift(shift);
            }
        }
    }

    const std::array<std::string, 12> skintypes = { "Light", "Tan",  "Dark",
                                                    "Pale",  "Blue", "Green",
                                                    "",      "",     "",
                                                    "Grey",  "Pink", "Red" };

    size_t index = skin;
    name_ = (index < skintypes.size()) ? skintypes[index] : "";
}

void Body::draw(Stance::Id stance,
                Layer layer,
                uint8_t frame,
                const DrawArgument &args) const {
    auto frameit = stances_[stance][layer].find(frame);

    if (frameit == stances_[stance][layer].end()) {
        return;
    }

    frameit->second.draw(args);
}

std::optional<Rectangle<int16_t>> Body::get_bounds(
    Stance::Id stance, uint8_t frame, const DrawArgument &args) const {
    std::optional<Rectangle<int16_t>> bounds;

    if (stance >= Stance::Id::LENGTH) {
        return bounds;
    }

    const auto include_layer = [&](Stance::Id layer_stance,
                                   Layer layer,
                                   uint8_t layer_frame) {
        const auto &textures = stances_[layer_stance][layer];
        const auto frameit = textures.find(layer_frame);

        if (frameit == textures.end()) {
            return;
        }

        const Texture &texture = frameit->second;

        if (!texture.is_valid()) {
            return;
        }

        const Point<int16_t> dimensions = texture.get_dimensions();

        if (dimensions.x() <= 0 || dimensions.y() <= 0) {
            return;
        }

        // Origins already include the constructor's stitching shifts.
        const auto rectangle =
            args.get_rectangle(texture.get_origin(), dimensions);
        const Rectangle<int16_t> normalized(
            std::min(rectangle.left(), rectangle.right()),
            std::max(rectangle.left(), rectangle.right()),
            std::min(rectangle.top(), rectangle.bottom()),
            std::max(rectangle.top(), rectangle.bottom()));

        if (!bounds) {
            bounds = normalized;
        } else {
            bounds = Rectangle<int16_t>(
                std::min(bounds->left(), normalized.left()),
                std::max(bounds->right(), normalized.right()),
                std::min(bounds->top(), normalized.top()),
                std::max(bounds->bottom(), normalized.bottom()));
        }
    };

    include_layer(stance, Layer::BODY, frame);
    include_layer(stance, Layer::HEAD, frame);

    if (stance == Stance::Id::DEAD) {
        // CharLook also draws the standing head in its DEAD branch.
        include_layer(Stance::Id::STAND1, Layer::HEAD, 1);
    } else if (!Stance::is_climbing(stance)) {
        // All arm/hand layers lie between BODY and HEAD; NONE is not drawn.
        for (int layer = Layer::ARM; layer < Layer::HEAD; ++layer) {
            include_layer(stance, static_cast<Layer>(layer), frame);
        }
    }

    return bounds;
}

const std::string &Body::get_name() const {
    return name_;
}

Body::Layer Body::layer_by_name(const std::string &name) {
    auto layer_iter = layers_by_name_.find(name);

    if (layer_iter == layers_by_name_.end()) {
        if (!name.empty()) {
            std::cout << "Unknown Body::Layer name: [" << name << "]"
                      << std::endl;
        }

        return Body::Layer::NONE;
    }

    return layer_iter->second;
}

const std::unordered_map<std::string, Body::Layer> Body::layers_by_name_ = {
    { "body", Body::Layer::BODY },
    { "backBody", Body::Layer::BODY },
    { "arm", Body::Layer::ARM },
    { "armBelowHead", Body::Layer::ARM_BELOW_HEAD },
    { "armBelowHeadOverMailChest", Body::Layer::ARM_BELOW_HEAD_OVER_MAIL },
    { "armOverHair", Body::Layer::ARM_OVER_HAIR },
    { "armOverHairBelowWeapon", Body::Layer::ARM_OVER_HAIR_BELOW_WEAPON },
    { "handBelowWeapon", Body::Layer::HAND_BELOW_WEAPON },
    { "handOverHair", Body::Layer::HAND_OVER_HAIR },
    { "handOverWeapon", Body::Layer::HAND_OVER_WEAPON },
    { "head", Body::Layer::HEAD }
};
}  // namespace ms