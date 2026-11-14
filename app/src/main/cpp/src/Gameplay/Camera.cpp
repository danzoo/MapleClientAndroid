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
#include "Camera.h"

#include <algorithm>

#include "../Constants.h"

namespace ms {
Camera::Camera() {
    x_.set(0.0);
    y_.set(0.0 + Y_OFFSET);

    VWIDTH = Constants::Constants::get().get_viewwidth();
    VHEIGHT = Constants::Constants::get().get_viewheight();
}

void Camera::update(Point<int16_t> position) {
    int32_t new_width = Constants::Constants::get().get_viewwidth();
    int32_t new_height = Constants::Constants::get().get_viewheight();

    if (VWIDTH != new_width || VHEIGHT != new_height) {
        VWIDTH = new_width;
        VHEIGHT = new_height;
    }

    double next_x = x_.get();
    double hdelta = VWIDTH / 2 - position.x() - next_x;

    // The further the target is ahead - a teleport, or a fall outrunning the follow - the faster the
    // camera closes, so a long jump of the character is caught up in a handful of ticks instead of
    // drifting after him. The boost only starts past a hop's worth of delta: a jump in place must
    // not bob the camera, and walking keeps the old gentle gain because its delta stays small.
    const double hgain =
        12.0 + std::max(0.0, std::min(std::abs(hdelta), 600.0) - 120.0) * 0.25;

    if (std::abs(hdelta) >= 5.0) {
        next_x += hdelta * (hgain / VWIDTH);
    }

    double next_y = y_.get();
    double vdelta = VHEIGHT / 2 - position.y() + Y_OFFSET - next_y;

    const double vgain =
        12.0 + std::max(0.0, std::min(std::abs(vdelta), 600.0) - 120.0) * 0.25;

    if (std::abs(vdelta) >= 5.0) {
        next_y += vdelta * (vgain / VHEIGHT);
    }

    // A map whose viewport range is smaller than the view can never fill it. Upstream parks it
    // against the range's first edge, which leaves all of the empty margin on the right and below
    // and the whole picture in the corner of the screen; splitting the margin between both edges
    // centres it instead. Each axis is decided on its own, so a map that is only too narrow keeps
    // its normal vertical follow.
    if (hbounds_.length() < VWIDTH) {
        next_x = hbounds_.first() + (VWIDTH - hbounds_.length()) / 2;
    } else if (next_x > hbounds_.first()) {
        next_x = hbounds_.first();
    } else if (next_x < hbounds_.second() + VWIDTH) {
        next_x = hbounds_.second() + VWIDTH;
    }

    if (vbounds_.length() < VHEIGHT) {
        next_y = vbounds_.first() + (VHEIGHT - vbounds_.length()) / 2;
    } else if (next_y > vbounds_.first()) {
        next_y = vbounds_.first();
    } else if (next_y < vbounds_.second() + VHEIGHT) {
        next_y = vbounds_.second() + VHEIGHT;
    }

    x_ = next_x;
    y_ = next_y;
}

void Camera::set_position(Point<int16_t> position) {
    int32_t new_width = Constants::Constants::get().get_viewwidth();
    int32_t new_height = Constants::Constants::get().get_viewheight();

    if (VWIDTH != new_width || VHEIGHT != new_height) {
        VWIDTH = new_width;
        VHEIGHT = new_height;
    }

    x_.set(VWIDTH / 2 - position.x());
    y_.set(VHEIGHT / 2 - position.y() + Y_OFFSET);
}

void Camera::set_view(Range<int16_t> mapwalls, Range<int16_t> mapborders) {
    hbounds_ = -mapwalls;
    vbounds_ = -mapborders;
}

Point<int16_t> Camera::position() const {
    auto shortx = static_cast<int16_t>(std::round(x_.get()));
    auto shorty = static_cast<int16_t>(std::round(y_.get()));

    return { shortx, shorty };
}

Point<int16_t> Camera::position(float alpha) const {
    auto interx = static_cast<int16_t>(std::round(x_.get(alpha)));
    auto intery = static_cast<int16_t>(std::round(y_.get(alpha)));

    return { interx, intery };
}

Point<double> Camera::real_position(float alpha) const {
    return { x_.get(alpha), y_.get(alpha) };
}
}  // namespace ms