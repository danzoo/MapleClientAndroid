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
#include "Foothold.h"

#include <cmath>
#include <limits>

namespace ms {
Foothold::Foothold(const img::node &src, uint16_t id, uint8_t ly) :
    m_id_(id),
    m_prev_(src["prev"]),
    m_next_(src["next"]),
    m_layer_(ly),
    m_force_(0.0),
    m_horizontal_(src["x1"], src["x2"]),
    m_vertical_(src["y1"], src["y2"]) {
    const int32_t width = hdelta();
    if (m_id_ && width != 0) {
        m_slope_ = static_cast<double>(vdelta()) / width;
        m_unit_cos_ = 1.0 / std::sqrt(1.0 + m_slope_ * m_slope_);
        m_unit_sin_ = m_slope_ * m_unit_cos_;
    }

    const img::node force = src["force"];

    if (force && (force.data_type() == img::node::type::integer
                  || force.data_type() == img::node::type::real)) {
        m_force_ = force.get_real();
    }
}

Foothold::Foothold() :
    m_id_(0),
    m_prev_(0),
    m_next_(0),
    m_layer_(0),
    m_force_(0.0) {}

uint16_t Foothold::id() const {
    return m_id_;
}

uint16_t Foothold::prev() const {
    return m_prev_;
}

uint16_t Foothold::next() const {
    return m_next_;
}

uint8_t Foothold::layer() const {
    return m_layer_;
}

const Range<int16_t> &Foothold::horizontal() const {
    return m_horizontal_;
}

const Range<int16_t> &Foothold::vertical() const {
    return m_vertical_;
}

int16_t Foothold::l() const {
    return m_horizontal_.smaller();
}

int16_t Foothold::r() const {
    return m_horizontal_.greater();
}

int16_t Foothold::t() const {
    return m_vertical_.smaller();
}

int16_t Foothold::b() const {
    return m_vertical_.greater();
}

int16_t Foothold::x1() const {
    return m_horizontal_.first();
}

int16_t Foothold::x2() const {
    return m_horizontal_.second();
}

int16_t Foothold::y1() const {
    return m_vertical_.first();
}

int16_t Foothold::y2() const {
    return m_vertical_.second();
}

bool Foothold::is_wall() const {
    return m_id_ && m_horizontal_.empty();
}

bool Foothold::is_floor() const {
    return m_id_ && m_vertical_.empty();
}

bool Foothold::is_left_edge() const {
    return m_id_ && m_prev_ == 0;
}

bool Foothold::is_right_edge() const {
    return m_id_ && m_next_ == 0;
}

bool Foothold::hcontains(int16_t x) const {
    return m_id_ && l() <= x && x <= r();
}

bool Foothold::vcontains(int16_t y) const {
    return m_id_ && t() <= y && y <= b();
}

bool Foothold::is_blocking(const Range<int16_t> &vertical) const {
    // Compare normalized bounds: a wall segment may store y1 > y2, and Range::overlaps/contains
    // assume an ascending range, so a body whose height window lies strictly inside a reversed
    // segment's span would otherwise read as not blocking and slip through the wall.
    return is_wall() && t() <= vertical.greater() && vertical.smaller() <= b();
}

bool Foothold::blocks_foot_height(double foot_y) const {
    if (!std::isfinite(foot_y)) {
        return false;
    }
    const double pixel_y = std::trunc(foot_y);
    return is_wall() && t() <= pixel_y - 1.0 && pixel_y - 50.0 <= b();
}

int32_t Foothold::hdelta() const {
    return static_cast<int32_t>(x2()) - static_cast<int32_t>(x1());
}

int32_t Foothold::vdelta() const {
    return static_cast<int32_t>(y2()) - static_cast<int32_t>(y1());
}

double Foothold::slope() const {
    return m_slope_;
}

double Foothold::unit_cos() const {
    return m_unit_cos_;
}

double Foothold::unit_sin() const {
    return m_unit_sin_;
}

double Foothold::force() const {
    return m_force_;
}

bool Foothold::is_slippery() const {
    return m_id_ && m_force_ != 0.0;
}

double Foothold::ground_below(double x) const {
    if (!m_id_) {
        return std::numeric_limits<double>::infinity();
    }

    return is_floor() ? y1() : slope() * (x - x1()) + y1();
}
}  // namespace ms