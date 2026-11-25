#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include "../../Template/Rectangle.h"

namespace ms {
// World-space target-selection geometry. Rectangular attacks retain their existing overlap test.
class AttackHitbox {
public:
    explicit AttackHitbox(const Rectangle<int16_t> &range) : rectangle_(range) {}

    // Keep both horizontal endpoints and the far edge of the resolved rectangle unchanged.
    // The near edge is centered vertically on the attack origin, not on the projectile muzzle.
    static AttackHitbox trapezoid(const Rectangle<int16_t> &range,
                                  int16_t origin_y,
                                  bool toleft,
                                  int16_t near_half_height) {
        AttackHitbox hitbox(range);
        hitbox.trapezoid_ = true;
        hitbox.valid_ = range.left() <= range.right() && range.top() <= range.bottom()
                        && near_half_height >= 0;

        const int32_t far_x = toleft ? range.left() : range.right();
        const int32_t near_x = toleft ? range.right() : range.left();
        const int32_t near_top = static_cast<int32_t>(origin_y) - near_half_height;
        const int32_t near_bottom = static_cast<int32_t>(origin_y) + near_half_height;
        hitbox.vertices_ = { Point<int32_t>(far_x, range.top()),
                             Point<int32_t>(near_x, near_top),
                             Point<int32_t>(near_x, near_bottom),
                             Point<int32_t>(far_x, range.bottom()) };
        return hitbox;
    }

    // Expand to the resolved top/bottom over a short near segment, then keep that
    // full height for the remaining range. A shorter range uses only a trapezoid.
    // Test the two pieces as a union: asymmetric fallback ranges can produce a
    // concave outline, so treating the whole outline as one convex SAT polygon is unsafe.
    static AttackHitbox trapezoid_with_rectangular_tail(const Rectangle<int16_t> &range,
                                                       int16_t origin_y,
                                                       bool toleft,
                                                       int16_t near_half_height,
                                                       int16_t taper_length) {
        AttackHitbox hitbox = trapezoid(range, origin_y, toleft, near_half_height);
        if (!hitbox.valid_ || taper_length < 0) {
            hitbox.valid_ = false;
            return hitbox;
        }
        // A zero transition length explicitly disables tapering.
        if (taper_length == 0) {
            return AttackHitbox(range);
        }

        const int32_t width = static_cast<int32_t>(range.right()) - range.left();
        const int32_t transition = std::min<int32_t>(width, taper_length);
        if (transition == width) {
            return hitbox;
        }
        const int32_t near_x = toleft ? range.right() : range.left();
        const int16_t join_x = static_cast<int16_t>(
            near_x + (toleft ? -transition : transition));
        const Rectangle<int16_t> near_range = toleft
            ? Rectangle<int16_t>(join_x, range.right(), range.top(), range.bottom())
            : Rectangle<int16_t>(range.left(), join_x, range.top(), range.bottom());
        hitbox = trapezoid(near_range, origin_y, toleft, near_half_height);
        hitbox.tail_ = toleft
            ? Rectangle<int16_t>(range.left(), join_x, range.top(), range.bottom())
            : Rectangle<int16_t>(join_x, range.right(), range.top(), range.bottom());
        hitbox.has_tail_ = true;
        return hitbox;
    }

    bool overlaps(const Rectangle<int16_t> &target) const {
        if (!trapezoid_) {
            return rectangle_.overlaps(target);
        }
        if (!valid_ || target.left() > target.right() || target.top() > target.bottom()) {
            return false;
        }
        if (has_tail_ && tail_.overlaps(target)) {
            return true;
        }

        const std::array<Point<int32_t>, 4> corners {
            Point<int32_t>(target.left(), target.top()),
            Point<int32_t>(target.right(), target.top()),
            Point<int32_t>(target.right(), target.bottom()),
            Point<int32_t>(target.left(), target.bottom())
        };

        // The rectangle axes also provide a bounding-box rejection and handle zero-width shapes.
        if (separated(corners, 1, 0) || separated(corners, 0, 1)) {
            return false;
        }
        // Separating-axis test for the sloped sides: target centers need not lie inside the shape.
        // Strict separation makes touching any edge or vertex count as an intersection.
        for (size_t i = 0; i < vertices_.size(); ++i) {
            const auto &from = vertices_[i];
            const auto &to = vertices_[(i + 1) % vertices_.size()];
            const int64_t axis_x = static_cast<int64_t>(from.y()) - to.y();
            const int64_t axis_y = static_cast<int64_t>(to.x()) - from.x();
            if (separated(corners, axis_x, axis_y)) {
                return false;
            }
        }
        return true;
    }

private:
    bool separated(const std::array<Point<int32_t>, 4> &corners,
                   int64_t axis_x,
                   int64_t axis_y) const {
        const auto project = [axis_x, axis_y](const Point<int32_t> &point) {
            return axis_x * point.x() + axis_y * point.y();
        };
        int64_t shape_min = project(vertices_[0]);
        int64_t shape_max = shape_min;
        int64_t target_min = project(corners[0]);
        int64_t target_max = target_min;
        for (size_t i = 1; i < vertices_.size(); ++i) {
            const int64_t shape = project(vertices_[i]);
            const int64_t target = project(corners[i]);
            shape_min = std::min(shape_min, shape);
            shape_max = std::max(shape_max, shape);
            target_min = std::min(target_min, target);
            target_max = std::max(target_max, target);
        }
        return shape_max < target_min || target_max < shape_min;
    }

    Rectangle<int16_t> rectangle_;
    Rectangle<int16_t> tail_;
    std::array<Point<int32_t>, 4> vertices_ {};
    bool trapezoid_ = false;
    bool has_tail_ = false;
    bool valid_ = true;
};
}  // namespace ms
