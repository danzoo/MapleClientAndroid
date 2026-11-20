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
#include <vector>

#include "PhysicsParams.h"

namespace ms {
FootholdTree::FootholdTree(const img::node &src) {
    const int coordinate_min = std::numeric_limits<int16_t>::min();
    const int coordinate_max = std::numeric_limits<int16_t>::max();
    int leftw = coordinate_max;
    int rightw = coordinate_min;
    int botb = coordinate_min;
    int topb = coordinate_max;

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

                if (id == 0) {
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

                const int start = foothold.l();
                const int end = foothold.r();

                for (int i = start; i <= end; i++) {
                    footholds_by_x_.emplace(static_cast<int16_t>(i), id);
                }
            }
        }
    }

    if (footholds_.empty()) {
        return;
    }

    const int wall_margin = std::min(25, (rightw - leftw) / 2);
    walls_ = {static_cast<int16_t>(leftw + wall_margin),
              static_cast<int16_t>(rightw - wall_margin)};
    borders_ = {static_cast<int16_t>(std::clamp(topb - 300, coordinate_min, coordinate_max)),
                static_cast<int16_t>(std::clamp(botb + 100, coordinate_min, coordinate_max))};
}

FootholdTree::FootholdTree() = default;

void FootholdTree::limit_movement(PhysicsObject &phobj) const {
    limit_movement(phobj, phobj.hstep(), phobj.vstep());
}

void FootholdTree::limit_movement(PhysicsObject &phobj, double hmove, double vmove) const {
    prepare_drop(phobj);
    limit_movement(phobj, hmove, vmove, 0);
}

void FootholdTree::limit_movement(PhysicsObject &phobj, double hmove, double vmove,
                                 uint16_t landed_fhid) const {
    if (phobj.hmobile()) {
        double crnt_x = phobj.crnt_x();
        double next_x = crnt_x + hmove;

        bool left = phobj.hspeed < 0.0f;
        bool collision = false;
        double wall = 0.0;

        // Grounded queries retain the nearby check and follow only the swept floor spans.
        // Airborne motion follows the current landing platform's chain far enough to reach
        // the side wall and all of its connected segments.
        // Ice uses the same ground integrator and the same swept-terrain queries as NORMAL;
        // only the surface coefficients differ, so both types take this path.
        if (phobj.onground || phobj.is_ground_type()) {
            const uint16_t wall_fhid = phobj.is_ground_type() && landed_fhid
                ? landed_fhid : phobj.fhid;
            const bool airborne = phobj.is_ground_type()
                && !landed_fhid && (!phobj.onground || phobj.vspeed < 0.0);
            if (airborne) {
                auto hits_at_contact = [&](double candidate) {
                    const bool crosses = left ? crnt_x >= candidate && next_x <= candidate
                                              : crnt_x <= candidate && next_x >= candidate;
                    if (!crosses) {
                        return false;
                    }

                    const double fraction = hmove != 0.0 ? (candidate - crnt_x) / hmove : 0.0;
                    const double contact_y = phobj.crnt_y() + vmove * fraction;
                    return get_air_wall(wall_fhid, left, contact_y) == candidate;
                };

                // The body can cross the wall before reaching this piece's end height.
                // Look up candidates at both heights, then recheck the wall's height window
                // at the actual crossing. The start lookup also catches a wall lost by the end query.
                wall = get_air_wall(wall_fhid, left, phobj.crnt_y() + vmove);
                collision = hits_at_contact(wall);
                if (!collision && vmove != 0.0) {
                    wall = get_air_wall(wall_fhid, left, phobj.crnt_y());
                    collision = hits_at_contact(wall);
                }
            } else {
                // A single substep can cross several very short floors before their wall.
                // Keep the nearby query first, then follow only as far as this movement needs.
                // Both queries must use the support's height at the wall, even when
                // this tick first walks onto a later short floor in the same chain.
                const PhysicsObject *supporting = phobj.is_ground_type() && phobj.onground
                    && phobj.vspeed == 0.0 && vmove == 0.0 ? &phobj : nullptr;
                wall = get_air_wall(wall_fhid, left, phobj.crnt_y() + vmove, next_x, supporting);
                collision = left ? crnt_x >= wall && next_x <= wall
                                 : crnt_x <= wall && next_x >= wall;
            }
        }

        // Side walls belong to the body's linked terrain, not every platform whose
        // endpoint this sweep crosses. Globally adopting an overhead or unrelated
        // platform's wall turns existing passages into solid barriers.

        // Releasing a floor is not permission to cross its associated wall.
        // Ground lookup may already refer to a lower floor, so check the original
        // component too, at the actual contact height, and keep the nearest hit.
        if (phobj.drop_from_fhid) {
            for (double fy : {phobj.crnt_y() + vmove, phobj.crnt_y()}) {
                const double candidate = get_air_wall(phobj.drop_from_fhid, left, fy);
                const bool crosses = left ? crnt_x >= candidate && next_x <= candidate
                                          : crnt_x <= candidate && next_x >= candidate;
                const double fraction = hmove != 0.0 ? (candidate - crnt_x) / hmove : 0.0;
                const double contact_y = phobj.crnt_y() + vmove * fraction;
                if (crosses && get_air_wall(phobj.drop_from_fhid, left, contact_y) == candidate
                    && (!collision || (left ? candidate > wall : candidate < wall))) {
                    wall = candidate;
                    collision = true;
                }
            }
        }

        if (!collision
            && phobj.is_flag_set(PhysicsObject::Flag::TURN_AT_EDGES)) {
            wall = get_edge(phobj.fhid, left);
            collision = left ? crnt_x >= wall && next_x <= wall
                             : crnt_x <= wall && next_x >= wall;
        }

        if (collision) {
            // Stay on the incoming side: at an exact shared endpoint the next ground lookup
            // may pick the platform beyond the wall and lose the blocking link. Moving away
            // still works because the wall is then behind the swept segment.
            phobj.limitx(std::nextafter(wall, crnt_x));
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

    bool landed = false;

    if (phobj.vmobile()) {
        double crnt_y = phobj.crnt_y();
        double next_y = crnt_y + vmove;

        // Measure the drop against the ground actually below the swept columns, not against
        // phobj.fhid. After a rope leap the foothold may be frozen from the grab or left at zero
        // when no terrain was found. A stale foothold can report ground the body is already past,
        // while a null foothold has no ground. Querying the terrain under the path finds the
        // platform the body should land on regardless of its stored foothold.
        const uint16_t start_fhid = find_fhid_below(phobj.crnt_x(), crnt_y,
                                                   &phobj, hmove, vmove);
        const uint16_t dest_fhid = find_fhid_below(phobj.crnt_x() + hmove, crnt_y,
                                                  &phobj, hmove, vmove);
        double ground_start = get_fh(start_fhid).ground_below(phobj.crnt_x());
        double ground_dest = get_fh(dest_fhid).ground_below(phobj.crnt_x() + hmove);
        double ground = std::min(ground_start, ground_dest);

        bool collision = vmove > 0.0 && crnt_y <= ground && next_y >= ground;

        if (collision) {
            // Land on the surface under where the piece ends, not the highest one it crossed. On a
            // downhill the two differ by the slope's drop over the piece, and landing on the higher
            // one leaves the body above the ground at its own x: update_fh then never calls it
            // grounded again, so a slide runs to the bottom of the slope with neither friction nor
            // input. A destination further below than the slope accounts for is another floor - an
            // edge, a platform below - and that one is still met at the crossing point.
            double drop = std::abs(get_fh(start_fhid).slope()) * std::abs(hmove);
            const bool lands_at_dest = ground_dest - ground <= drop + 0.5;
            const uint16_t landing_fhid = lands_at_dest ? dest_fhid : start_fhid;

            // A landing replaces the horizontal speed in two cases:
            //   * nothing steers the body (the standing-jump-on-a-slope slide),
            //   * a DIFFERENT direction was pressed while airborne. Then the
            //     client's own projection + opposite-input clamp stops the old
            //     momentum so the new direction takes over at once. This is a
            //     user-specified jump/movement isolation rule, marked as such
            //     in REPORT.md; it is not a PC-measured constant.
            //
            // A direction held continuously through the jump keeps its speed,
            // because applying the impact projection there is what made a
            // running jump lose its momentum (flat: 125 -> 62.5, uphill: clamped
            // to 0) and made the slope feel impossible to reach.
            //
            // was_airborne is essential: this crossing test also holds on every
            // standing and walking tick, because gravity keeps vspeed > 0 and
            // crnt_y <= ground <= next_y. Gating on the onground false->true
            // edge makes the conversion a one-shot EVENT.
            const Foothold &landing_fh = get_fh(landing_fhid);
            const bool steer_changed = phobj.air_direction_changed;
            const bool no_steer = phobj.landing_input_dir == 0.0;
            if (phobj.vspeed > 0.0 && landing_fh.id() && phobj.was_airborne
                && phobj.is_ground_type() && (no_steer || steer_changed)) {
                phobj.landing_conversion = true;
                phobj.landing_fhid = landing_fhid;
                phobj.landing_vx = phobj.hspeed;
                phobj.landing_vy = phobj.vspeed;
                phobj.landing_fhcos = landing_fh.unit_cos();
                phobj.landing_fhsin = landing_fh.unit_sin();
            }

            phobj.limity(lands_at_dest ? ground_dest : ground);
            phobj.fhid = landing_fhid;
            phobj.onground = landing_fh.id() && !landing_fh.is_wall();

            // On slopes y already refers to landing_x, while x has not advanced yet. Carry the
            // selected surface into the recursive wall check instead of querying mismatched x/y.
            // After x is applied, update_fh follows this support at the actual x, including
            // when that wall check shortened the horizontal piece or the body left its edge.
            limit_movement(phobj, hmove, 0.0, landing_fhid);
            landed = true;
        } else {
            if (next_y < borders_.first()) {
                phobj.limity(borders_.first());
            } else if (next_y > borders_.second()) {
                phobj.limity(borders_.second());

                // Below the lowest terrain there is nothing left to collide with: every wall span
                // ends far above, so a body that keeps its speed here slides under the whole map and
                // comes out past the hill it fell beside - which from the inside looks like walking
                // through it. Stop it in the pit instead.
                phobj.hspeed = 0.0;
            }
        }
    }

    // Ground that rises to meet a body in the air is met sideways, not fallen onto. The hop a
    // knockback gives lifts less than the slope it shoves the body up, so the feet end the piece
    // below the surface; the fall test above only fires while the ground is still under the body,
    // so from then on nothing lands it again and it drops through the hillside. A settled body
    // is left out only when its ground engine follows the support. Flying, swimming and
    // floating bodies still need the sweep at contact, as do launches with upward speed.
    if (!landed && (!phobj.onground || phobj.vspeed < 0.0 || !phobj.is_ground_type())) {
        double crossx = 0.0;
        double crossy = 0.0;

        if (swept_ground(phobj.crnt_x(), phobj.crnt_y(),
                         phobj.crnt_x() + hmove, phobj.crnt_y() + vmove,
                         crossx, crossy, phobj)) {
            phobj.limitx(crossx);
            phobj.limity(crossy);
        }
    }
}

bool FootholdTree::swept_ground(double fromx, double fromy, double tox, double toy,
                                double &crossx, double &crossy,
                                const PhysicsObject &object) const {
    if (fromx == tox) {
        return false;
    }

    const int direction = tox > fromx ? 1 : -1;
    const double span = tox - fromx;

    // The piece is a straight segment and so is every surface under it, so the gap between the two
    // changes sign at most once. Sampling the whole columns it passes plus the point it ends at
    // therefore catches every crossing, including one in the last fraction of a column.
    auto meet = [&](double x, double y) {
        if (!std::isfinite(x) || !std::isfinite(y)
            || x < std::numeric_limits<int16_t>::min()
            || x > std::numeric_limits<int16_t>::max()) {
            return false;
        }
        const Foothold *hit = nullptr;
        double best = 0.0;

        auto range = footholds_by_x_.equal_range(static_cast<int16_t>(x));

        for (auto iter = range.first; iter != range.second; ++iter) {
            const Foothold &fh = footholds_.at(iter->second);
            if (object.ignores_drop_floor(fh.id())) {
                continue;
            }
            // The integer column can select a platform just beyond a fractional endpoint.
            // A sweep may only meet a surface inside its actual horizontal span.
            if (x < fh.l() || x > fh.r()) {
                continue;
            }
            const double ground = fh.ground_below(x);

            // Standing on a surface is not meeting it. The touch-down frame leaves the body exactly
            // at ground level and then re-checks the horizontal travel still left in the piece, so
            // counting equality here would stop every landing dead and take its walking speed with
            // it. Only a body that ends the piece under the surface has run into rising ground.
            if (y <= ground) {
                continue;
            }

            // Terrain is one-way. Compare the body and surface at the SAME point:
            // when this piece enters a new foothold's span, its starting height is
            // not its height at that endpoint. Using fromy there can misclassify
            // a hop from a lower neighbouring slope as rising through this one.
            const double refx = std::clamp(fromx,
                                           static_cast<double>(fh.l()),
                                           static_cast<double>(fh.r()));
            const double refy = fromy + (toy - fromy) * (refx - fromx) / span;

            if (refy > fh.ground_below(refx)) {
                continue;
            }

            // Stacked platforms: the one met first is the highest of those the body has reached.
            if (!hit || ground < best) {
                hit = &fh;
                best = ground;
            }
        }

        if (!hit) {
            return false;
        }

        crossx = x;
        crossy = best;
        return true;
    };

    // Only the columns the segment actually crosses. A piece is often shorter than one column - a
    // walking body covers a fraction of a pixel per tick - and stepping to the next integer anyway
    // would sample past the destination, where the interpolation below extrapolates a y the body
    // never reaches and invents a collision. floor/ceil pick the first integer strictly ahead of the
    // start and the last one still short of the end, for either direction and either sign of x.
    const double first = direction > 0 ? std::floor(fromx) + 1.0 : std::ceil(fromx) - 1.0;
    const double last = direction > 0 ? std::floor(tox) : std::ceil(tox);

    std::vector<double> samples;

    for (double x = first; direction > 0 ? x <= last : x >= last; x += direction) {
        samples.push_back(x);
    }

    samples.push_back(tox);

    // A ledge narrower than the piece is stepped over between two column samples: the surface only
    // exists inside its own span, so neither the columns beside it nor the piece's end see it, and a
    // body falling fast enough slips through. Also test where the path enters and leaves every span
    // it touches.
    const double lo = std::min(fromx, tox);
    const double hi = std::max(fromx, tox);
    const size_t column_count = samples.size();

    for (size_t i = 0; i < column_count; i++) {
        auto range = footholds_by_x_.equal_range(static_cast<int16_t>(samples[i]));

        for (auto iter = range.first; iter != range.second; ++iter) {
            const Foothold &fh = footholds_.at(iter->second);

            for (double e : {static_cast<double>(fh.l()), static_cast<double>(fh.r())}) {
                if (e >= lo && e <= hi) {
                    samples.push_back(e);
                }
            }
        }
    }

    std::sort(samples.begin(), samples.end(), [direction](double a, double b) {
        return direction > 0 ? a < b : a > b;
    });
    samples.erase(std::unique(samples.begin(), samples.end()), samples.end());

    for (double x : samples) {
        const double y = fromy + (toy - fromy) * (x - fromx) / span;

        if (meet(x, y)) {
            return true;
        }
    }

    return false;
}

void FootholdTree::prepare_drop(PhysicsObject &object) const {
    if (!object.drop_from_fhid || !object.drop_footholds.empty()) {
        return;
    }
    const Foothold &start = get_fh(object.drop_from_fhid);
    if (!start.id() || start.is_wall()) {
        object.clear_drop();
        return;
    }

    object.drop_footholds.push_back(start.id());
    for (bool left : {true, false}) {
        const Foothold *cur = &start;
        for (size_t remaining = footholds_.size(); remaining > 0; --remaining) {
            const Foothold &next = get_fh(left ? cur->prev() : cur->next());
            if (!next.id() || next.is_wall() || next.layer() != start.layer()) {
                break;
            }
            const int16_t edge_x = left ? cur->l() : cur->r();
            const int16_t edge_y = cur->x1() == edge_x ? cur->y1() : cur->y2();
            const int16_t next_x = left ? next.r() : next.l();
            const int16_t next_y = next.x1() == next_x ? next.y1() : next.y2();
            if (next_x != edge_x || next_y != edge_y
                || (left ? next.l() >= cur->l() : next.r() <= cur->r())
                || std::find(object.drop_footholds.begin(), object.drop_footholds.end(),
                             next.id()) != object.drop_footholds.end()) {
                break;
            }
            object.drop_footholds.push_back(next.id());
            cur = &next;
        }
    }
    std::sort(object.drop_footholds.begin(), object.drop_footholds.end());
}

void FootholdTree::finish_drop(PhysicsObject &object) const {
    if (!object.drop_from_fhid) {
        return;
    }
    if (object.onground && !object.ignores_drop_floor(object.fhid)) {
        object.clear_drop();
        return;
    }

    double left = std::numeric_limits<double>::infinity();
    double right = -std::numeric_limits<double>::infinity();
    for (uint16_t id : object.drop_footholds) {
        const Foothold &fh = get_fh(id);
        left = std::min(left, static_cast<double>(fh.l()));
        right = std::max(right, static_cast<double>(fh.r()));
    }
    // Keep the original wall association until landing or leaving the component;
    // a wall may extend below its floor. The map bottom is also a safe endpoint.
    // A timer alone would restore a long downhill too early. No other platform
    // or wall is disabled while falling.
    if (object.crnt_x() < left || object.crnt_x() > right
        || object.crnt_y() >= borders_.second()) {
        object.clear_drop();
    }
}

void FootholdTree::update_fh(PhysicsObject &phobj, bool follow_ground) const {
    if (phobj.type == PhysicsObject::Type::FIXATED && phobj.fhid > 0) {
        phobj.clear_drop();
        return;
    }
    prepare_drop(phobj);

    const double x = phobj.crnt_x();
    const double y = phobj.crnt_y();
    const Foothold &curfh = get_fh(phobj.fhid);
    const Foothold *support = &curfh;
    bool supported = phobj.is_ground_type() && phobj.onground && phobj.vspeed == 0.0
        && curfh.id() && !curfh.is_wall() && !phobj.ignores_drop_floor(curfh.id())
        && (follow_ground || (x >= curfh.l() && x <= curfh.r()
                               && y == curfh.ground_below(x)));

    if (supported) {
        support = &connected_support(phobj, x);
        supported = support->id() != 0;
    }

    if (supported) {
        phobj.fhid = support->id();
    } else {
        phobj.onground = false;
        phobj.fhid = get_fhid_below(x, y, &phobj);
    }

    const Foothold &nextfh = get_fh(phobj.fhid);
    if (!nextfh.id()) {
        phobj.fhid = 0;
        phobj.fhslope = 0.0;
        phobj.fhcos = 1.0;
        phobj.fhsin = 0.0;
        phobj.foothold_force = 0.0;
        phobj.surface_friction = 1.0;
        phobj.onground = false;
        phobj.enablejd = false;
        if (phobj.is_ground_type()) {
            phobj.type = PhysicsObject::Type::NORMAL;
        }
        finish_drop(phobj);
        return;
    }

    phobj.fhslope = nextfh.slope();
    phobj.fhcos = nextfh.unit_cos();
    phobj.fhsin = nextfh.unit_sin();

    // Refresh the surface every tick. A WZ foothold `force` is a conveyor/slide push; the
    // Data tree has no separate friction key, so this client treats those footholds as the
    // slippery ones and gives them the archive's minFriction. Only ground bodies are re-typed:
    // flying, swimming and floating bodies keep the engine their state selected.
    phobj.foothold_force = nextfh.force();
    phobj.surface_friction = nextfh.is_slippery()
                                 ? PhysicsParams::min_friction()
                                 : 1.0;

    if (phobj.is_ground_type()) {
        phobj.type = nextfh.is_slippery() ? PhysicsObject::Type::ICE
                                          : PhysicsObject::Type::NORMAL;
    }

    const double ground = nextfh.ground_below(x);

    if (supported) {
        // Ground following is geometric, independent of speed sign and slope change.
        // Seat exactly while preserving the interpolation origin: a delta addition can
        // round above or below this height, which is not a different one-way contact.
        phobj.y.shift_to(ground);
        phobj.onground = true;
    } else {
        // A spawn, airborne lookup or broken connection is not permission to snap down.
        phobj.onground = phobj.y == ground;
    }

    finish_drop(phobj);

    // A newly landed player can receive Down+Jump before its next state update.
    // Refresh eligibility with the actual contact, not a false cache that waits
    // for the following STAND/WALK/PRONE tick to set CHECK_BELOW.
    if (phobj.onground || phobj.enablejd || phobj.is_flag_set(PhysicsObject::Flag::CHECK_BELOW)) {
        uint16_t belowid = get_fhid_below(x, ground + 1.0, &phobj);

        if (belowid > 0) {
            // How far the next platform down lies says nothing about whether the drop is allowed:
            // a leap off a high ledge is still a leap, and the fall itself lands or re-enters the
            // map. Only a column with nothing below it - the map's own floor - refuses the drop.
            phobj.enablejd = true;
            phobj.groundbelow = ground + 1.0;
        } else {
            phobj.enablejd = false;
        }

        phobj.clear_flag(PhysicsObject::Flag::CHECK_BELOW);
    }

    if (phobj.fhlayer == 0 || phobj.onground) {
        phobj.fhlayer = nextfh.layer();
    }
}

const Foothold &FootholdTree::connected_support(const PhysicsObject &object, double x) const {
    const Foothold *support = &get_fh(object.fhid);
    if (!std::isfinite(x) || !support->id() || support->is_wall()
        || object.ignores_drop_floor(support->id())) {
        return null_fh_;
    }

    // Do not replace this support with an unrelated floor below the destination.
    // Follow exactly the chain that update_fh can seat after a checked substep.
    size_t remaining = footholds_.size();
    while (x < support->l() || x > support->r()) {
        if (remaining == 0) {
            return null_fh_;
        }
        --remaining;

        const bool left = x < support->l();
        const Foothold &next = get_fh(left ? support->prev() : support->next());
        if (!next.id() || next.is_wall() || next.layer() != support->layer()
            || object.ignores_drop_floor(next.id())) {
            return null_fh_;
        }

        const int16_t edge_x = left ? support->l() : support->r();
        const int16_t edge_y = support->x1() == edge_x
            ? support->y1() : support->y2();
        const int16_t next_edge_x = left ? next.r() : next.l();
        const int16_t next_edge_y = next.x1() == next_edge_x
            ? next.y1() : next.y2();
        if (next_edge_x != edge_x || next_edge_y != edge_y
            || (left ? next.l() >= support->l() : next.r() <= support->r())) {
            return null_fh_;
        }
        support = &next;
    }
    return *support;
}

const Foothold &FootholdTree::get_fh(uint16_t fhid) const {
    auto iter = footholds_.find(fhid);

    if (iter == footholds_.end()) {
        return null_fh_;
    }

    return iter->second;
}

double FootholdTree::get_air_wall(uint16_t curid, bool left, double fy,
                                  double to_x, const PhysicsObject *supporting) const {
    const double boundary = left ? walls_.first() : walls_.second();
    const double nearby = get_wall(curid, left, fy, supporting);

    // Preserve every result of the existing two-neighbour query before filling its omissions.
    if (nearby != boundary) {
        return nearby;
    }

    const Foothold *cur = &get_fh(curid);
    if (!cur->id() || cur->is_wall()) {
        return boundary;
    }

    bool wall_started = false;
    int16_t wall_x = 0;

    // Reach the side wall through this landing platform's connected floor chain. Once there,
    // check the whole same-column wall chain, not only the two pieces closest to the floor.
    // Endpoint/layer checks exclude unrelated geometry; the budget also bounds malformed cycles.
    for (size_t remaining = footholds_.size(); remaining > 0; --remaining) {
        // Grounded motion only needs the floors the substep can actually reach. Still
        // inspect a wall exactly at the destination and all its same-column segments.
        if (!wall_started && std::isfinite(to_x)
            && (left ? to_x > cur->l() : to_x < cur->r())) {
            break;
        }
        const Foothold &next = get_fh(left ? cur->prev() : cur->next());
        if (!next.id() || next.layer() != cur->layer()) {
            break;
        }

        // Wall endpoint order is independent of prev/next traversal. Accept either shared
        // endpoint without changing the stored endpoint order or the traversal direction.
        const bool connected = next.is_wall() || cur->is_wall()
            ? ((next.x1() == cur->x1() && next.y1() == cur->y1())
               || (next.x1() == cur->x2() && next.y1() == cur->y2())
               || (next.x2() == cur->x1() && next.y2() == cur->y1())
               || (next.x2() == cur->x2() && next.y2() == cur->y2()))
            : (left ? next.x2() == cur->x1() && next.y2() == cur->y1()
                    : next.x1() == cur->x2() && next.y1() == cur->y2());
        if (!connected) {
            break;
        }

        if (wall_started) {
            // Do not search beyond this wall into another platform or another wall column.
            if (!next.is_wall() || next.x1() != wall_x) {
                break;
            }
        } else if (next.is_wall()) {
            wall_started = true;
            wall_x = next.x1();
        } else if (left ? next.l() >= cur->l() : next.r() <= cur->r()) {
            break;
        }

        if (wall_started) {
            const Foothold &support = supporting
                ? connected_support(*supporting, wall_x) : null_fh_;
            const double foot_y = support.id() ? support.ground_below(wall_x) : fy;
            if (next.blocks_foot_height(foot_y)) {
                return wall_x;
            }
        }
        cur = &next;
    }

    return boundary;
}

double FootholdTree::get_wall(uint16_t curid, bool left, double fy,
                              const PhysicsObject *supporting) const {
    const double boundary = left ? walls_.first() : walls_.second();
    const Foothold *cur = &get_fh(curid);
    if (!cur->id() || cur->is_wall()) {
        return boundary;
    }

    bool wall_started = false;
    int16_t wall_x = 0;

    // Keep the two-neighbour fast path, but only accept real connected geometry.
    for (int remaining = 2; remaining > 0; --remaining) {
        const Foothold &next = get_fh(left ? cur->prev() : cur->next());
        if (!next.id() || next.layer() != cur->layer()) {
            break;
        }

        const bool connected = next.is_wall() || cur->is_wall()
            ? ((next.x1() == cur->x1() && next.y1() == cur->y1())
               || (next.x1() == cur->x2() && next.y1() == cur->y2())
               || (next.x2() == cur->x1() && next.y2() == cur->y1())
               || (next.x2() == cur->x2() && next.y2() == cur->y2()))
            : (left ? next.x2() == cur->x1() && next.y2() == cur->y1()
                    : next.x1() == cur->x2() && next.y1() == cur->y2());
        if (!connected) {
            break;
        }

        if (wall_started) {
            // Once a wall begins, do not follow another floor or wall column.
            if (!next.is_wall() || next.x1() != wall_x) {
                break;
            }
        } else if (next.is_wall()) {
            wall_started = true;
            wall_x = next.x1();
        } else if (left ? next.l() >= cur->l() : next.r() <= cur->r()) {
            break;
        }

        if (wall_started) {
            const Foothold &support = supporting
                ? connected_support(*supporting, wall_x) : null_fh_;
            const double foot_y = support.id() ? support.ground_below(wall_x) : fy;
            if (next.blocks_foot_height(foot_y)) {
                return next.x1();
            }
        }
        cur = &next;
    }

    return boundary;
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

uint16_t FootholdTree::get_fhid_below(double fx, double fy,
                                      const PhysicsObject *object) const {
    return find_fhid_below(fx, fy, object);
}

uint16_t FootholdTree::find_fhid_below(double fx, double fy, const PhysicsObject *object,
                                       double hmove, double vmove) const {
    const double coordinate_min = std::numeric_limits<int16_t>::min();
    const double coordinate_max = std::numeric_limits<int16_t>::max();
    if (!std::isfinite(fx) || !std::isfinite(fy)
        || fx < coordinate_min || fx > coordinate_max
        || fy < coordinate_min || fy > coordinate_max) {
        return 0;
    }

    uint16_t ret = 0;
    double comp = borders_.second();

    auto x = static_cast<int16_t>(fx);
    auto range = footholds_by_x_.equal_range(x);

    for (auto iter = range.first; iter != range.second; ++iter) {
        const Foothold &fh = footholds_.at(iter->second);
        if (fx < fh.l() || fx > fh.r()
            || (object && object->ignores_drop_floor(fh.id()))) {
            continue;
        }

        if (object && std::isfinite(hmove)) {
            const double fromx = object->crnt_x();
            const double refx = std::clamp(fromx, static_cast<double>(fh.l()),
                                           static_cast<double>(fh.r()));
            const double refy = object->crnt_y()
                + (hmove != 0.0 ? vmove * (refx - fromx) / hmove : 0.0);
            // A destination column's ground can lie below fromy even though
            // the whole body path is below this slope. Compare at the entry x,
            // just as the sideways sweep does, before selecting this candidate.
            if (refy > fh.ground_below(refx)) {
                continue;
            }
        }

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

    const int direction = to.x() > from.x() ? 1 : -1;
    const int last = include_end ? to.x() : to.x() - direction;
    const double slope =
        static_cast<double>(to.y() - from.y()) / (to.x() - from.x());

    for (int x = from.x() + direction;
         direction > 0 ? x <= last : x >= last;
         x += direction) {
        const double y = from.y() + slope * (x - from.x());
        const int16_t crossed = static_cast<int16_t>(std::round(y));
        // Every visited column lies between the int16 endpoints; the int loop can
        // advance past an extreme endpoint without wrapping before its condition.
        const int16_t column = static_cast<int16_t>(x);
        auto range = walls_by_x_.equal_range(column);

        for (auto iter = range.first; iter != range.second; ++iter) {
            if (get_fh(iter->second).vcontains(crossed)) {
                cross = Point<int16_t>(column, crossed);
                return true;
            }
        }
    }

    return false;
}
}  // namespace ms