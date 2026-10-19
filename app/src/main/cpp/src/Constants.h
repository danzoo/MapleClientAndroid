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

#include <cmath>
#include <cstdint>

#include "Template/Point.h"
#include "Template/Singleton.h"

namespace ms::Constants {
// Timestep, e.g. the granularity in which the game advances.
constexpr uint16_t TIMESTEP = 8;

// Fraction of real time the simulation advances at. Movement, jumps and animations are all
// driven by the timestep, so scaling it slows them together and keeps every trajectory intact;
// lowering walk speed or jump force directly would shorten the jumps instead.
constexpr double SIMULATION_SPEED = 0.85;

// Downward acceleration of a free falling object, in pixels per timestep squared. Anything that
// predicts a trajectory has to use the same value the integrator applies.
constexpr double GRAVITY = 0.14;

// How long loot that a mob dropped stays hidden after its packet arrives, on top of however long
// that mob is still waiting for the blow that killed it. The server sends the loot before it sends
// the kill, so without a wait the items are already flying out of a mob that is still standing.
constexpr int16_t DROP_DELAY = 40;

// Longest a mob flinches before the death animation starts, in timesteps. Only reached if the
// flinch never reports that it has finished playing.
constexpr int16_t HIT_DEATH_WAIT = 40;

// How far short of the end of a platform a knocked-back mob is held. Mobs hang off the point they
// are stored at, so keeping this much clearance stops them from ending up over the gap they would
// fall into.
constexpr int16_t PLATFORM_EDGE_MARGIN = 8;

// How much bigger the interface is drawn than the world around it. Everything the UI lays out is
// measured in a screen that is this much smaller than the real one and then magnified, so raising
// it grows every window, button and piece of text together while the map stays the same size.
constexpr float UI_SCALE = 1.5f;

// The interface is laid out on a screen UI_SCALE smaller than the view, so a placement made
// against the view - a window's stored origin, for instance - has to be brought into that space
// before the interface can use it. Writing it back out goes the other way.
constexpr Point<int16_t> to_ui_space(const Point<int16_t> &viewpos) {
    return Point<int16_t>(static_cast<int16_t>(viewpos.x() / UI_SCALE),
                          static_cast<int16_t>(viewpos.y() / UI_SCALE));
}

constexpr Point<int16_t> to_view_space(const Point<int16_t> &uipos) {
    return Point<int16_t>(static_cast<int16_t>(uipos.x() * UI_SCALE),
                          static_cast<int16_t>(uipos.y() * UI_SCALE));
}

class Constants : public Singleton<Constants> {
public:
    Constants() : VIEWWIDTH(1366), VIEWHEIGHT(768) {}

    ~Constants() override = default;

    int16_t get_viewwidth() { return VIEWWIDTH; }

    void set_viewwidth(int16_t width) { VIEWWIDTH = width; }

    int16_t get_viewheight() { return VIEWHEIGHT; }

    void set_viewheight(int16_t height) { VIEWHEIGHT = height; }

    // The size of the screen the interface is laid out on. Positions and sizes given in this
    // space are magnified by UI_SCALE on their way to the frame, so a window that has to reach an
    // edge has to be anchored in here rather than in the view size. Rounded up so that an element
    // which is made to fill this screen still covers the view once it is magnified.
    int16_t get_ui_viewwidth() { return static_cast<int16_t>(std::ceil(VIEWWIDTH / UI_SCALE)); }

    int16_t get_ui_viewheight() { return static_cast<int16_t>(std::ceil(VIEWHEIGHT / UI_SCALE)); }

private:
    // Window and screen width.
    int16_t VIEWWIDTH;
    // Window and screen height.
    int16_t VIEWHEIGHT;
};
}  // namespace ms::Constants