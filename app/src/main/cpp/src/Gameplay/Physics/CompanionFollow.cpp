#include "CompanionFollow.h"

#include <cassert>
#include <cmath>

#include "../../Constants.h"
#include "Physics.h"

namespace ms {
CompanionFollowLayout::CompanionFollowLayout(Point<double> spacing) : spacing_(spacing) {
    assert(std::isfinite(spacing_.x()) && spacing_.x() > 0.0);
    assert(std::isfinite(spacing_.y()) && spacing_.y() > 0.0);
}

CompanionFollowLayout::Slot CompanionFollowLayout::acquire() {
    for (Slot slot = 0; slot < occupied_.size(); ++slot) {
        if (!occupied_[slot]) {
            occupied_[slot] = true;
            return slot;
        }
    }
    occupied_.push_back(true);
    return occupied_.size() - 1;
}

void CompanionFollowLayout::release(Slot slot) {
    assert(slot < occupied_.size() && occupied_[slot]);
    if (slot < occupied_.size()) {
        occupied_[slot] = false;
    }
}

Point<double> CompanionFollowLayout::offset(Slot slot) const {
    assert(slot < occupied_.size() && occupied_[slot]);
    if (slot == 0) {
        return {};
    }
    const double distance = static_cast<double>(slot / 2 + slot % 2);
    const double direction = slot % 2 == 1 ? -1.0 : 1.0;
    return {direction * distance * spacing_.x(), direction * distance * spacing_.y()};
}

CompanionFollow::CompanionFollow(const Config &config) : config_(config) {
    assert(std::isfinite(config_.spawn_x));
    assert(std::isfinite(config_.float_y));
    assert(std::isfinite(config_.follow_drive) && config_.follow_drive >= 0.0);
    assert(std::isfinite(config_.follow_deadzone) && config_.follow_deadzone >= 0.0);
    assert(std::isfinite(config_.max_distance)
           && config_.max_distance >= config_.follow_deadzone);
    assert(std::isfinite(config_.height_tau) && config_.height_tau > 0.0);
    height_ease_ = 1.0 - std::exp(-Constants::TIMESTEP / (1000.0 * config_.height_tau));
}

void CompanionFollow::reset(Point<int16_t> owner, Point<double> layout_offset) {
    phobj_.set_x(owner.x() + config_.spawn_x + layout_offset.x());
    phobj_.set_y(owner.y() + config_.float_y + layout_offset.y());
    phobj_.hspeed = 0.0;
    phobj_.vspeed = 0.0;
    phobj_.hforce = 0.0;
    phobj_.vforce = 0.0;
    phobj_.type = PhysicsObject::Type::FLYING;
}

void CompanionFollow::update(const Physics &physics, Point<int16_t> owner, bool climbing,
                             Point<double> layout_offset) {
    const double target_x = owner.x() + layout_offset.x();
    const double target_y = owner.y() + config_.float_y + layout_offset.y();
    const double eased_y = phobj_.crnt_y() + (target_y - phobj_.crnt_y()) * height_ease_;

    if (climbing) {
        // Hanging on a rope must never reposition the companion horizontally, even
        // if the owner snaps to the rope or exceeds the normal follow leash.
        phobj_.type = PhysicsObject::Type::FIXATED;
        phobj_.set_y(eased_y);
        phobj_.hspeed = 0.0;
        phobj_.vspeed = 0.0;
        phobj_.hforce = 0.0;
        phobj_.vforce = 0.0;
        physics.move_object(phobj_);
        phobj_.set_y(eased_y);
        return;
    }

    phobj_.type = PhysicsObject::Type::FLYING;
    const double dx = target_x - phobj_.get_position().x();
    phobj_.hforce = std::abs(dx) > config_.follow_deadzone
                       ? (dx > 0.0 ? config_.follow_drive : -config_.follow_drive)
                       : 0.0;
    phobj_.set_y(eased_y);
    phobj_.vspeed = 0.0;
    phobj_.vforce = 0.0;

    physics.move_object(phobj_);
    phobj_.set_y(eased_y);

    const double gap = phobj_.get_position().x() - target_x;
    if (gap > config_.max_distance) {
        phobj_.set_x(target_x + config_.max_distance);
        phobj_.hspeed = 0.0;
    } else if (gap < -config_.max_distance) {
        phobj_.set_x(target_x - config_.max_distance);
        phobj_.hspeed = 0.0;
    }
}

Point<int16_t> CompanionFollow::get_position() const {
    return phobj_.get_position();
}

Point<int16_t> CompanionFollow::get_absolute(double viewx, double viewy, float alpha) const {
    return phobj_.get_absolute(viewx, viewy, alpha);
}
}  // namespace ms
