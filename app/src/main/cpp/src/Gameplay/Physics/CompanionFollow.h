// Configurable local follow motion for floating companions.
#pragma once

#include <cstddef>
#include <vector>

#include "PhysicsObject.h"

namespace ms {
class Physics;

// Acquire once on spawn and release once on removal. Live slots never move when
// others leave; a new companion reuses the first vacant slot. Copies are independent.
class CompanionFollowLayout {
public:
    using Slot = std::size_t;

    explicit CompanionFollowLayout(Point<double> spacing = {36.0, 24.0});

    Slot acquire();
    void release(Slot slot);
    Point<double> offset(Slot slot) const;

private:
    Point<double> spacing_;
    std::vector<bool> occupied_;
};

// Owns movement only; the caller manages animation, lifetime and network authority.
// This is a floating follow policy, not every Summon movement type.
class CompanionFollow {
public:
    struct Config {
        double spawn_x;
        double float_y;
        double follow_drive;
        double follow_deadzone;
        double max_distance;
        double height_tau;
    };

    explicit CompanionFollow(const Config &config);

    // Use the same owner's layout.offset(slot) at spawn and on every update.
    void reset(Point<int16_t> owner, Point<double> layout_offset = {});

    // Climbing keeps the current X exactly: no horizontal chase or leash correction.
    // Ground follow resumes after leaving the rope, including its horizontal leash.
    void update(const Physics &physics, Point<int16_t> owner, bool climbing,
                Point<double> layout_offset = {});

    Point<int16_t> get_position() const;
    Point<int16_t> get_absolute(double viewx, double viewy, float alpha) const;

private:
    Config config_;
    double height_ease_;
    PhysicsObject phobj_;
};
}  // namespace ms
