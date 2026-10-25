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
#pragma once

#include "../../Constants.h"
#include "../../Template/Interpolated.h"
#include "../../Template/Point.h"

namespace ms {
// Positions are in pixels, speeds are in pixels per second and forces are in pixels per second
// squared - the same units Map/Physics.img uses - so a value read from the archive is assigned
// without being translated first. The simulation still only runs once per timestep, which is where
// the step() accessors come in: they are the distance actually covered in one update.
struct MovingObject {
    Linear<double> x;
    Linear<double> y;
    double hspeed = 0.0;
    double vspeed = 0.0;

    void normalize() {
        x.normalize();
        y.normalize();
    }

    void move() {
        x += hstep();
        y += vstep();
    }

    double hstep() const { return hspeed * Constants::TICK_SECONDS; }

    double vstep() const { return vspeed * Constants::TICK_SECONDS; }

    // A step quoted in pixels per tick, which is the unit every hand-written speed in this client
    // names. hstep() above is the same speed read back out in the tick the loop runs, so what is
    // stored stays in seconds and only the call site speaks in ticks.
    void set_hstep(double pixels_per_quoted_tick) {
        hspeed = pixels_per_quoted_tick / Constants::TICK_SECONDS;
    }

    void set_vstep(double pixels_per_quoted_tick) {
        vspeed = pixels_per_quoted_tick / Constants::TICK_SECONDS;
    }

    void set_x(double d) { x.set(d); }

    void set_y(double d) { y.set(d); }

    void limitx(double d) {
        x = d;
        hspeed = 0.0;
    }

    void limity(double d) {
        y = d;
        vspeed = 0.0;
    }

    // Distance in pixels, delay in milliseconds of game time: the quotient is a speed in pixels per
    // real second, which is what makes the move take the delay it names whatever the timestep is.
    void movexuntil(double d, uint16_t delay) {
        if (delay) {
            hspeed = (d - x.get()) * Constants::SIMULATION_SPEED * 1000 / delay;
        }
    }

    void moveyuntil(double d, uint16_t delay) {
        if (delay) {
            vspeed = (d - y.get()) * Constants::SIMULATION_SPEED * 1000 / delay;
        }
    }

    bool hmobile() const { return hspeed != 0.0; }

    bool vmobile() const { return vspeed != 0.0; }

    bool mobile() const { return hmobile() || vmobile(); }

    double crnt_x() const { return x.get(); }

    double crnt_y() const { return y.get(); }

    double next_x() const { return x + hstep(); }

    double next_y() const { return y + vstep(); }

    int16_t get_x() const {
        double rounded = std::round(x.get());
        return static_cast<int16_t>(rounded);
    }

    int16_t get_y() const {
        double rounded = std::round(y.get());
        return static_cast<int16_t>(rounded);
    }

    int16_t get_last_x() const {
        double rounded = std::round(x.last());
        return static_cast<int16_t>(rounded);
    }

    int16_t get_last_y() const {
        double rounded = std::round(y.last());
        return static_cast<int16_t>(rounded);
    }

    Point<int16_t> get_position() const { return { get_x(), get_y() }; }

    int16_t get_absolute_x(double viewx, float alpha) const {
        double interx = x.normalized() ? std::round(x.get()) : x.get(alpha);

        return static_cast<int16_t>(std::round(interx + viewx));
    }

    int16_t get_absolute_y(double viewy, float alpha) const {
        double intery = y.normalized() ? std::round(y.get()) : y.get(alpha);

        return static_cast<int16_t>(std::round(intery + viewy));
    }

    Point<int16_t> get_absolute(double viewx, double viewy, float alpha) const {
        return { get_absolute_x(viewx, alpha), get_absolute_y(viewy, alpha) };
    }
};

// Structure that contains all properties necessary for physics calculations
struct PhysicsObject : public MovingObject {
    // Determines which physics engine to use
    enum Type { NORMAL, ICE, SWIMMING, FLYING, FIXATED };

    enum Flag {
        NO_GRAVITY = 0x0001,
        TURN_AT_EDGES = 0x0002,
        CHECK_BELOW = 0x0004
    };

    Type type = Type::NORMAL;
    int32_t flags = 0;
    uint16_t fhid = 0;
    double fhslope = 0.0;
    int8_t fhlayer = 0;
    double groundbelow = 0.0;
    bool onground = true;
    bool enablejd = false;

    // Accelerations applied by whatever is steering this object, and the sums the integrator
    // builds from them, both in pixels per second squared.
    double hforce = 0.0;
    double vforce = 0.0;
    double hacc = 0.0;
    double vacc = 0.0;

    // How far one force alone carries the object during a single timestep, in pixels. This is the
    // term that a step projection has to add to hstep() to see where the object will be next tick.
    double hpush() const {
        return hforce * Constants::TICK_SECONDS * Constants::TICK_SECONDS;
    }

    double vpush() const {
        return vforce * Constants::TICK_SECONDS * Constants::TICK_SECONDS;
    }

    bool is_flag_set(Flag f) { return (flags & f) != 0; }

    bool is_flag_not_set(Flag f) { return !is_flag_set(f); }

    void set_flag(Flag f) { flags |= f; }

    void clear_flag(Flag f) { flags &= ~f; }

    void clear_flags() { flags = 0; }
};

// The numbers this client was written with before the physics archive became the source of them are
// all still per timestep. Anything left over from that era is restated here rather than by hand, so
// that the only unit a reader has to hold in mind while looking at a physics field is the second.
//
// The tick they are quoted in is the one the loop runs, Constants::TICK_SECONDS, so the round trip
// through here lands back on the literal: a speed of 3 put in comes out as 3 pixels per update. What
// the conversion buys is that the fields themselves are all in seconds, which is the unit the
// archive speaks, and that SIMULATION_SPEED can still be moved without rewriting them by hand.
namespace tick_units {
    inline double velocity(double pixels_per_tick) {
        return pixels_per_tick / Constants::TICK_SECONDS;
    }

    inline double accel(double pixels_per_tick_squared) {
        return pixels_per_tick_squared
            / (Constants::TICK_SECONDS * Constants::TICK_SECONDS);
    }
}  // namespace tick_units
}  // namespace ms