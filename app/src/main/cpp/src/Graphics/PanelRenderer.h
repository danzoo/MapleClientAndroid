//	This file is part of the continued Journey MMORPG client
//	Copyright (C) 2015-2024  Daniel Allendorf, Ryan Payton, Bizhou Xing
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

#include <cstdint>
#include <vector>

#include "GLES3/gl32.h"

#include "../Template/Singleton.h"
#include "GraphicsGL.h"

namespace ms {
// Owns the panel render pass end to end: its geometry batch, its framebuffer, and the projection
// that lays its windows out on a screen PANEL_SCALE smaller than the view. The third pass next to
// the world and the interface, built the same way the interface is but sharing nothing with it, so
// the windows drawn here can be magnified past what the interface's own scale gives without
// touching the size of anything else on the screen - and without disturbing the batching, the glyph
// strip or the eviction of the interface those windows sit inside.
//
// A pass opened from inside the interface's own bracket takes precedence for as long as it stays
// open, so one window switches over by bracketing only its own drawing.
//
// Its target is composited over the interface, not under it: a window that opts into this pass is
// the thing being looked at, so it covers whatever the interface happens to draw where it stands.
// Anything that has to stay above an enlarged window belongs to the game's own interface rather than
// to the screen such a window is standing on.
class PanelRenderer : public Singleton<PanelRenderer> {
public:
    PanelRenderer() = default;

    // Route every draw queued from here until end() into the panel batch.
    void begin() { active_ = true; }

    void end() { active_ = false; }

    // True while draws belong to the panel rather than to the interface or the scene. GraphicsGL's
    // batch and atlas accessors read this to decide which vector a draw lands in.
    bool active() const { return active_; }

    std::vector<GraphicsGL::Quad> &quads() { return panel_quads_; }

    std::vector<GraphicsGL::Circle> &circles() { return panel_circles_; }

    // The size, in real surface pixels, of the letterboxed view box the panels are composited over.
    // Set every frame from Window::update_viewport. The target is built at this size and the batch is
    // mapped onto it from the PANEL_SCALE-smaller layout space, so a panel is rasterised at native
    // pixel density at its own magnification and end_scene() only has to blit it one-to-one.
    void set_native_size(int32_t width, int32_t height) {
        native_width_ = width;
        native_height_ = height;
    }

    // Submit the panel batch into its own target, then hand the scene pass back the state it left
    // (framebuffer, viewport, program, atlas, attribute pointers).
    void flush();

    // The composite step in GraphicsGL::end_scene reads these to blit the panels over the
    // interface.
    bool empty() const {
        return panel_quads_.empty() && panel_circles_.empty();
    }

    GLuint texture() const { return panel_tex_; }

    // Drop the queued geometry. Called with the scene batch at the start of every frame.
    void clear() {
        panel_quads_.clear();
        panel_circles_.clear();
    }

private:
    bool active_ = false;

    // Geometry and lettering in one vector, in the order they were drawn, so a panel opened later
    // covers the text of a panel opened earlier. GraphicsGL::flush_ui splits the run by Quad::text.
    std::vector<GraphicsGL::Quad> panel_quads_;
    std::vector<GraphicsGL::Circle> panel_circles_;

    // Native surface size of the view box, fed in each frame; 0 until the first viewport update.
    int32_t native_width_ = 0;
    int32_t native_height_ = 0;

    GLuint panel_fbo_ = 0;
    GLuint panel_tex_ = 0;
    // Current size the target texture/FBO is built at (native pixels).
    int16_t panel_width_ = 0;
    int16_t panel_height_ = 0;
};
}  // namespace ms
