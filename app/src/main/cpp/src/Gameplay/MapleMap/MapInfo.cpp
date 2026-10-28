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
#include "MapInfo.h"

#include <cmath>

#include "../../Constants.h"
#include "../Physics/Foothold.h"

namespace ms {
MapInfo::MapInfo(const img::node &src,
                 Range<int16_t> walls,
                 Range<int16_t> borders) {
    img::node info = src["info"];

    if (info["VRLeft"].data_type() == img::node::type::integer) {
        map_walls_ = Range<int16_t>(info["VRLeft"], info["VRRight"]);
        map_borders_ = Range<int16_t>(info["VRTop"], info["VRBottom"]);
    } else {
        map_walls_ = walls;
        map_borders_ = borders;
    }

    std::string bgmpath = info["bgm"];
    size_t split = bgmpath.find('/');
    bgm_ = bgmpath.substr(0, split) + ".img/" + bgmpath.substr(split + 1);

    cloud_ = info["cloud"].get_bool();
    field_limit_ = info["fieldLimit"];
    hide_minimap_ = info["hideMinimap"].get_bool();
    map_mark_ = std::string(info["mapMark"]);
    swim_ = info["swim"].get_bool();
    town_ = info["town"].get_bool();

    for (const auto &seat : src["seat"]) {
        seats_.push_back(seat);
    }

    for (const auto &ladder : src["ladderRope"]) {
        ladders_.emplace_back(ladder, src["foothold"]);
    }
}

MapInfo::MapInfo() = default;

bool MapInfo::is_underwater() const {
    return swim_;
}

std::string MapInfo::get_bgm() const {
    return bgm_;
}

Range<int16_t> MapInfo::get_walls() const {
    return map_walls_;
}

Range<int16_t> MapInfo::get_borders() const {
    return map_borders_;
}

std::optional<std::reference_wrapper<const Seat>> MapInfo::findseat(
    Point<int16_t> position) const {
    for (const auto &seat : seats_) {
        if (seat.inrange(position)) {
            return seat;
        }
    }

    return {};
}

std::optional<std::reference_wrapper<const Ladder>> MapInfo::findladder(
    Point<int16_t> position,
    bool upwards,
    bool onground) const {
    for (const auto &ladder : ladders_) {
        if (ladder.inrange(position, upwards, onground)) {
            return ladder;
        }
    }

    return {};
}

Seat::Seat(const img::node &src) {
    pos_ = src;
}

bool Seat::inrange(Point<int16_t> position) const {
    auto hor = Range<int16_t>::symmetric(position.x(), 10);
    auto ver = Range<int16_t>::symmetric(position.y(), 10);

    return hor.contains(pos_.x()) && ver.contains(pos_.y());
}

Point<int16_t> Seat::getpos() const {
    return pos_;
}

Ladder::Ladder(const img::node &src, const img::node &footholds) {
    x_ = src["x"];
    y1_ = src["y1"];
    y2_ = src["y2"];
    ladder_ = src["l"].get_bool();

    // The archive hangs a rope's top end a couple of pixels under the platform it is installed on,
    // and that platform - not the rope's own end - is what a climb finishes on. Which of the two the
    // top of a given rope has decides what the last pixels of an upward climb mean: a ledge to be
    // handed on to, or the end of a rope that stops in mid-air.
    //
    // Foothold reads an id of zero as "not a foothold at all" and answers false to every question
    // about its shape, so the ones taken out of the archive are numbered as they go by.
    uint16_t fhid = 1;

    for (const auto &layer : footholds) {
        for (const auto &range : layer) {
            for (const auto &item : range) {
                const Foothold fh(item, fhid++, 0);

                if (fh.is_wall() || !fh.hcontains(x_)) {
                    continue;
                }

                const int16_t ground =
                    static_cast<int16_t>(std::round(fh.ground_below(x_)));

                if (ground > y1_ || y1_ - ground > Constants::CLIMB_TOP_MOUNT_Y) {
                    continue;
                }

                if (top_mount_ == NO_TOP_MOUNT || ground > top_mount_) {
                    top_mount_ = ground;
                }
            }
        }
    }
}

bool Ladder::is_ladder() const {
    return ladder_;
}

bool Ladder::inrange(Point<int16_t> position,
                     bool upwards,
                     bool onground) const {
    // A rope reached from a platform and a rope passed in mid-air are aimed differently. Standing at
    // a ropehead with a joystick wants a forgiving window; leaping across one is not aimed at all,
    // and the forgiving window grabs it by accident. Only the airborne upward approach is held
    // narrow, because a grab may reach less far past the rope's end than Ladder::felloff measures
    // with, but never further.
    const bool narrow = upwards && !onground;
    const int16_t range_x = narrow ? Constants::CLIMB_RANGE_X_AIR
                                   : Constants::CLIMB_RANGE_X;
    const int16_t margin_y = narrow ? Constants::CLIMB_MARGIN_Y_AIR
                                    : Constants::CLIMB_MARGIN_Y;

    auto hor = Range<int16_t>::symmetric(position.x(), range_x);
    auto ver = Range<int16_t>(y1_, y2_);

    int16_t y = upwards ? position.y() - margin_y : position.y() + margin_y;

    return hor.contains(x_) && ver.contains(y);
}

bool Ladder::felloff(int16_t y, bool downwards) const {
    // The margin the grab downwards from a platform is measured with, and it has to be at least as
    // wide as any grab's: in every one of the shipped maps the rope's top sits exactly two pixels
    // below the platform it hangs from, so a character standing at the top is two pixels above y1.
    // Asked whether it has fallen off with a narrower margin than the grab used, the answer is yes
    // on the very frame the grab lands, which cancels the climb before it can move - the grab then
    // arrives again next frame, so the body plays the climbing animation without ever leaving the
    // platform.
    constexpr int16_t margin = Constants::CLIMB_MARGIN_Y;

    int16_t dy = downwards ? y + margin : y - margin;

    return dy > y2_ || y + margin < y1_;
}

int16_t Ladder::get_x() const {
    return x_;
}

int16_t Ladder::get_top() const {
    return y1_;
}

int16_t Ladder::get_top_mount() const {
    return top_mount_;
}
}  // namespace ms