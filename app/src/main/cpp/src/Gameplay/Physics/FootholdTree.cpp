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
#include "FootholdTree.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace ms {
FootholdTree::FootholdTree(const img::node &src) {
    int16_t leftw = 30000;
    int16_t rightw = -30000;
    int16_t botb = -30000;
    int16_t topb = 30000;

    for (const auto &basef : src) {
        uint8_t layer;

        try {
            layer = static_cast<uint8_t>(std::stoi(basef.name()));
        } catch (const std::exception &ex) {
            std::cout << __func__ << ": " << ex.what() << std::endl;
            continue;
        }

        for (const auto &midf : basef) {
            for (auto lastf : midf) {
                uint16_t id;

                try {
                    id = static_cast<uint16_t>(std::stoi(lastf.name()));
                } catch (const std::exception &ex) {
                    std::cout << __func__ << ": " << ex.what() << std::endl;
                    continue;
                }

                const Foothold &foothold =
                    footholds_
                        .emplace(std::piecewise_construct,
                                 std::forward_as_tuple(id),
                                 std::forward_as_tuple(lastf, id, layer))
                        .first->second;

                if (foothold.l() < leftw) {
                    leftw = foothold.l();
                }

                if (foothold.r() > rightw) {
                    rightw = foothold.r();
                }

                if (foothold.b() > botb) {
                    botb = foothold.b();
                }

                if (foothold.t() < topb) {
                    topb = foothold.t();
                }

                if (foothold.is_wall()) {
                    // A wall is one column wide, so it can't join the index of platforms below;
                    // it gets its own so a straight run can be tested against it.
                    walls_by_x_.emplace(foothold.x1(), id);
                    continue;
                }

                int16_t start = foothold.l();
                int16_t end = foothold.r();

                for (int i = start; i <= end; i++) {
                    footholds_by_x_.emplace(i, id);
                }
            }
        }
    }

    walls_ = { leftw + 25, rightw - 25 };
    borders_ = { topb - 300, botb + 100 };
}

FootholdTree::FootholdTree() = default;

void FootholdTree::limit_movement(PhysicsObject &phobj) const {
    limit_movement(phobj, phobj.hstep(), phobj.vstep());
}

void FootholdTree::limit_movement(PhysicsObject &phobj, double hmove, double vmove) const {
    if (phobj.hmobile()) {
        double crnt_x = phobj.crnt_x();
        double next_x = crnt_x + hmove;

        bool left = phobj.hspeed < 0.0f;
        double wall = get_wall(phobj.fhid, left, phobj.crnt_y() + vmove);
        bool collision = left ? crnt_x >= wall && next_x <= wall
                              : crnt_x <= wall && next_x >= wall;

        // get_wall only sees the walls joined to the object's own foothold, which is enough while
        // walking (the foothold walks along the chain until it sits beside the wall) but not in the
        // air: there the foothold is whatever floor is under the current x, mid-chain, so an enclosing
        // wall several links away is invisible and a jump carries the body straight through it. Sweep
        // the wall index over the columns this piece crosses instead, which finds the first wall in
        // the path no matter which foothold the body happens to reference.
        double swept = get_swept_wall(crnt_x, next_x, phobj.crnt_y() + vmove);
        if (!std::isnan(swept)) {
            wall = swept;
            collision = true;
        }

        if (!collision
            && phobj.is_flag_set(PhysicsObject::Flag::TURN_AT_EDGES)) {
            wall = get_edge(phobj.fhid, left);
            collision = left ? crnt_x >= wall && next_x <= wall
                             : crnt_x <= wall && next_x >= wall;
        }

        if (collision) {
            phobj.limitx(wall);
            phobj.clear_flag(PhysicsObject::Flag::TURN_AT_EDGES);
        } else if (left ? next_x < walls_.first() : next_x > walls_.second()) {
            // The crossing test above only fires while the wall is still ahead of the body. One
            // that started outside it (a spawn, a knockback, a rope release past the edge) would
            // otherwise keep walking away from the map forever.
            phobj.limitx(left ? walls_.first() : walls_.second());
        }
    }

    // Where the sweep above left the object is what the one below has to be measured from: if a
    // wall stopped it, there is no horizontal travel left in this piece to carry across the floor.
    if (!phobj.hmobile()) {
        hmove = 0.0;
    }

    if (phobj.vmobile()) {
        double crnt_y = phobj.crnt_y();
        double next_y = crnt_y + vmove;

        // Measure the drop against the ground actually below the swept columns, not against
        // phobj.fhid. After a rope leap the foothold is frozen from the grab (or left at zero by
        // update_fh's airborne early-out), and a stale or null foothold reports a ground the body is
        // already past - or NaN - so the crossing test never fires and the body sinks through the
        // platform it should have landed on. Querying the terrain under the path lands it regardless.
        double ground = std::min(ground_below_path(phobj.crnt_x(), crnt_y),
                                 ground_below_path(phobj.crnt_x() + hmove, crnt_y));

        bool collision = vmove > 0.0 && crnt_y <= ground && next_y >= ground;

        if (collision) {
            phobj.limity(ground);

            limit_movement(phobj, hmove, 0.0);
        } else {
            if (next_y < borders_.first()) {
                phobj.limity(borders_.first());
            } else if (next_y > borders_.second()) {
                phobj.limity(borders_.second());
            }
        }
    }
}

double FootholdTree::ground_below_path(double x, double y) const {
    uint16_t fhid = get_fhid_below(x, y);

    if (fhid == 0) {
        return std::numeric_limits<double>::infinity();
    }

    return get_fh(fhid).ground_below(x);
}

void FootholdTree::update_fh(PhysicsObject &phobj) const {
    if (phobj.type == PhysicsObject::Type::FIXATED && phobj.fhid > 0) {
        return;
    }

    const Foothold &curfh = get_fh(phobj.fhid);
    bool checkslope = false;

    double x = phobj.crnt_x();
    double y = phobj.crnt_y();

    if (phobj.onground) {
        if (std::floor(x) > curfh.r()) {
            phobj.fhid = curfh.next();
        } else if (std::ceil(x) < curfh.l()) {
            phobj.fhid = curfh.prev();
        }

        if (phobj.fhid == 0) {
            phobj.fhid = get_fhid_below(x, y);
        } else {
            checkslope = true;
        }
    } else {
        phobj.fhid = get_fhid_below(x, y);

        // fix for stuttering slope walk
        if (phobj.fhid == 0) {
            return;
        }
    }

    const Foothold &nextfh = get_fh(phobj.fhid);
    phobj.fhslope = nextfh.slope();

    double ground = nextfh.ground_below(x);

    if (phobj.vspeed == 0.0 && checkslope) {
        double vdelta = abs(phobj.fhslope);

        if (phobj.fhslope < 0.0) {
            vdelta *= (ground - y);
        } else if (phobj.fhslope > 0.0) {
            vdelta *= (y - ground);
        }

        if (curfh.slope() != 0.0 || nextfh.slope() != 0.0) {
            // vdelta is a number of pixels, so it is compared against the distance this tick will
            // actually cover rather than against the speed it is covered at.
            const double hstep = phobj.hstep();

            if (phobj.hspeed > 0.0 && vdelta <= hstep) {
                phobj.y = ground;
            } else if (phobj.hspeed < 0.0 && vdelta >= hstep) {
                phobj.y = ground;
            }
        }
    }

    phobj.onground = phobj.y == ground;

    if (phobj.enablejd || phobj.is_flag_set(PhysicsObject::Flag::CHECK_BELOW)) {
        uint16_t belowid = get_fhid_below(x, nextfh.ground_below(x) + 1.0);

        if (belowid > 0) {
            double nextground = get_fh(belowid).ground_below(x);
            phobj.enablejd = (nextground - ground) < 600.0;
            phobj.groundbelow = ground + 1.0;
        } else {
            phobj.enablejd = false;
        }

        phobj.clear_flag(PhysicsObject::Flag::CHECK_BELOW);
    }

    if (phobj.fhlayer == 0 || phobj.onground) {
        phobj.fhlayer = nextfh.layer();
    }

    if (phobj.fhid == 0) {
        phobj.fhid = curfh.id();
        phobj.limitx(curfh.x1());
    }
}

const Foothold &FootholdTree::get_fh(uint16_t fhid) const {
    auto iter = footholds_.find(fhid);

    if (iter == footholds_.end()) {
        return null_fh_;
    }

    return iter->second;
}

double FootholdTree::get_wall(uint16_t curid, bool left, double fy) const {
    auto shorty = static_cast<int16_t>(fy);
    Range<int16_t> vertical(shorty - 50, shorty - 1);
    const Foothold &cur = get_fh(curid);

    // A tall wall is stored as several collinear segments stacked along the foothold chain, so the
    // segment level with the body can be many links away - checking only the first two let a body
    // falling along the wall slip past it at any height above them. Walk the chain through
    // consecutive walls until one overlaps the body's height; stop at the first floor so the walk
    // stays bounded. This is what blocks a body pressed exactly against the wall column, which the
    // column sweep below starts past.
    for (uint16_t id = left ? cur.prev() : cur.next(); id != 0;) {
        const Foothold &fh = get_fh(id);

        if (!fh.is_wall()) {
            break;
        }

        if (fh.is_blocking(vertical)) {
            return fh.l();
        }

        id = left ? fh.prev() : fh.next();
    }

    return left ? walls_.first() : walls_.second();
}

double FootholdTree::get_swept_wall(double fromx, double tox, double fy) const {
    if (fromx == tox) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const int direction = tox > fromx ? 1 : -1;
    const int last = static_cast<int>(tox);
    auto shorty = static_cast<int16_t>(fy);
    Range<int16_t> vertical(shorty - 50, shorty - 1);

    for (int x = static_cast<int>(fromx) + direction;
         direction > 0 ? x <= last : x >= last;
         x += direction) {
        auto range = walls_by_x_.equal_range(static_cast<int16_t>(x));

        for (auto iter = range.first; iter != range.second; ++iter) {
            if (get_fh(iter->second).is_blocking(vertical)) {
                return static_cast<double>(x);
            }
        }
    }

    return std::numeric_limits<double>::quiet_NaN();
}

double FootholdTree::get_edge(uint16_t curid, bool left) const {
    const Foothold &fh = get_fh(curid);

    if (left) {
        uint16_t previd = fh.prev();

        if (!previd) {
            return fh.l();
        }

        const Foothold &prev = get_fh(previd);
        uint16_t prev_previd = prev.prev();

        if (!prev_previd) {
            return prev.l();
        }

        return walls_.first();
    }

    uint16_t nextid = fh.next();

    if (!nextid) {
        return fh.r();
    }

    const Foothold &next = get_fh(nextid);
    uint16_t next_nextid = next.next();

    if (!next_nextid) {
        return next.r();
    }

    return walls_.second();
}

uint16_t FootholdTree::get_fhid_below(double fx, double fy) const {
    uint16_t ret = 0;
    double comp = borders_.second();

    auto x = static_cast<int16_t>(fx);
    auto range = footholds_by_x_.equal_range(x);

    for (auto iter = range.first; iter != range.second; ++iter) {
        const Foothold &fh = footholds_.at(iter->second);
        double ycomp = fh.ground_below(fx);

        if (comp >= ycomp && ycomp >= fy) {
            comp = ycomp;
            ret = fh.id();
        }
    }

    return ret;
}

int16_t FootholdTree::get_y_below(Point<int16_t> position) const {
    if (uint16_t fhid = get_fhid_below(position.x(), position.y())) {
        const Foothold &fh = get_fh(fhid);

        return static_cast<int16_t>(fh.ground_below(position.x()));
    }

    return borders_.second();
}

Range<int16_t> FootholdTree::get_walls() const {
    return walls_;
}

const Foothold &FootholdTree::get_foothold(uint16_t fhid) const {
    return get_fh(fhid);
}

Range<int16_t> FootholdTree::get_borders() const {
    return borders_;
}

bool FootholdTree::blocks_line(Point<int16_t> from,
                               Point<int16_t> to,
                               Point<int16_t> &cross) const {
    return sweep_walls(from, to, false, cross);
}

bool FootholdTree::first_crossing(Point<int16_t> from,
                                  Point<int16_t> to,
                                  Point<int16_t> &cross) const {
    return sweep_walls(from, to, true, cross);
}

bool FootholdTree::sweep_walls(Point<int16_t> from,
                               Point<int16_t> to,
                               bool include_end,
                               Point<int16_t> &cross) const {
    if (from.x() == to.x()) {
        return false;
    }

    const int16_t direction = to.x() > from.x() ? 1 : -1;
    const int16_t last = include_end ? to.x() : to.x() - direction;
    const double slope =
        static_cast<double>(to.y() - from.y()) / (to.x() - from.x());

    for (int16_t x = from.x() + direction;
         direction > 0 ? x <= last : x >= last;
         x += direction) {
        const double y = from.y() + slope * (x - from.x());
        const int16_t crossed = static_cast<int16_t>(std::round(y));

        auto range = walls_by_x_.equal_range(x);

        for (auto iter = range.first; iter != range.second; ++iter) {
            if (get_fh(iter->second).vcontains(crossed)) {
                cross = Point<int16_t>(x, crossed);
                return true;
            }
        }
    }

    return false;
}
}  // namespace ms