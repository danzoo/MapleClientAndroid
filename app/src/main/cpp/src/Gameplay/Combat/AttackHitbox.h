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

    // The cone starts exactly at the physical feet, irrespective of the fallback
    // rectangle's near edge. Reach its top/bottom within taper_length, then keep
    // that full height up to its existing far edge. Shorter attacks use only a cone.
    // Test cone and tail as a union: asymmetric ranges can yield a concave outline.
    static AttackHitbox cone_with_rectangular_tail(const Rectangle<int16_t> &range,
                                                  Point<int16_t> origin,
                                                  bool toleft,
                                                  int16_t taper_length) {
        AttackHitbox hitbox(range);
        hitbox.tapered_ = true;
        const int32_t far_x = toleft ? range.left() : range.right();
        const int32_t length = toleft ? static_cast<int32_t>(origin.x()) - far_x
                                      : far_x - origin.x();
        hitbox.valid_ = range.left() <= range.right() && range.top() <= range.bottom()
                        && length >= 0 && taper_length >= 0;
        if (!hitbox.valid_) {
            return hitbox;
        }
        // A zero transition length explicitly disables tapering.
        if (taper_length == 0) {
            return AttackHitbox(range);
        }

        const int32_t transition = std::min<int32_t>(length, taper_length);
        const int16_t join_x = static_cast<int16_t>(
            origin.x() + (toleft ? -transition : transition));
        const Point<int32_t> apex(origin.x(), origin.y());
        hitbox.vertices_ = { Point<int32_t>(join_x, range.top()), apex, apex,
                             Point<int32_t>(join_x, range.bottom()) };
        if (transition < length) {
            hitbox.tail_ = toleft
                ? Rectangle<int16_t>(range.left(), join_x, range.top(), range.bottom())
                : Rectangle<int16_t>(join_x, range.right(), range.top(), range.bottom());
            hitbox.has_tail_ = true;
        }
        return hitbox;
    }

    bool overlaps(const Rectangle<int16_t> &target) const {
        if (!tapered_) {
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

#ifdef MS_DEBUG_HITBOXES
    // Enumerate the actual outline, not its bounding rectangle or the cone/tail seam.
    template<typename DrawEdge>
    void for_each_outline_edge(DrawEdge draw_edge) const {
        if (!tapered_) {
            const std::array<Point<int32_t>, 4> corners {
                Point<int32_t>(rectangle_.left(), rectangle_.top()),
                Point<int32_t>(rectangle_.right(), rectangle_.top()),
                Point<int32_t>(rectangle_.right(), rectangle_.bottom()),
                Point<int32_t>(rectangle_.left(), rectangle_.bottom())
            };
            for (size_t i = 0; i < corners.size(); ++i) {
                draw_edge(corners[i], corners[(i + 1) % corners.size()]);
            }
            return;
        }
        if (!valid_) {
            return;
        }

        const auto &top = vertices_[0];
        const auto &apex = vertices_[1];
        const auto &bottom = vertices_[3];
        draw_edge(apex, top);
        if (has_tail_) {
            const int32_t far_x = apex.x() > top.x() ? tail_.left() : tail_.right();
            const Point<int32_t> far_top(far_x, tail_.top());
            const Point<int32_t> far_bottom(far_x, tail_.bottom());
            draw_edge(top, far_top);
            draw_edge(far_top, far_bottom);
            draw_edge(far_bottom, bottom);
        } else {
            draw_edge(top, bottom);
        }
        draw_edge(bottom, apex);
    }
#endif

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
    bool tapered_ = false;
    bool has_tail_ = false;
    bool valid_ = true;
};
}  // namespace ms
