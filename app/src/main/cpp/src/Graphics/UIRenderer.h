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
// Owns the interface render pass end to end: the UI geometry batch, its own framebuffer, and the
// projection that lays the interface out on a screen UI_SCALE smaller than the view. Splitting it
// out of the scene renderer means the two never share batch state, and gives the interface its own
// place to grow (native-resolution target, its own glyph atlas) without touching the world drawing.
class UIRenderer : public Singleton<UIRenderer> {
public:
    UIRenderer() = default;

    // Route every draw queued from here until end() into the interface batch.
    void begin() { active_ = true; }

    void end() { active_ = false; }

    // True while draws belong to the interface rather than the scene. GraphicsGL's batch accessor
    // reads this to decide which vector a draw lands in.
    bool active() const { return active_; }

    std::vector<GraphicsGL::Quad> &quads() { return ui_quads_; }

    std::vector<GraphicsGL::Circle> &circles() { return ui_circles_; }

    // The size, in real surface pixels, of the letterboxed view box the interface is composited
    // over. Set every frame from Window::update_viewport. The interface target is built at this
    // size and its batch is mapped onto it from the UI_SCALE-smaller layout space, so the interface
    // is rasterised at native pixel density and end_scene() only has to blit it one-to-one.
    void set_native_size(int32_t width, int32_t height) {
        native_width_ = width;
        native_height_ = height;
    }

    // Submit the interface batch into its own target, then hand the scene pass back the state it
    // left (framebuffer, viewport, program, atlas, attribute pointers).
    void flush();

    // The composite step in GraphicsGL::end_scene reads these to blit the interface over the scene.
    bool empty() const {
        return ui_quads_.empty() && ui_circles_.empty();
    }

    GLuint texture() const { return ui_tex_; }

    // Drop the queued geometry. Called with the scene batch at the start of every frame.
    void clear() {
        ui_quads_.clear();
        ui_circles_.clear();
    }

private:
    bool active_ = false;

    // Geometry and lettering in one vector, in the order they were drawn, so a window opened later
    // covers the text of a window opened earlier. GraphicsGL::flush_ui splits the run by Quad::text.
    std::vector<GraphicsGL::Quad> ui_quads_;
    std::vector<GraphicsGL::Circle> ui_circles_;

    // Native surface size of the view box, fed in each frame; 0 until the first viewport update.
    int32_t native_width_ = 0;
    int32_t native_height_ = 0;

    GLuint ui_fbo_ = 0;
    GLuint ui_tex_ = 0;
    // Current size the target texture/FBO is built at (native pixels).
    int16_t ui_width_ = 0;
    int16_t ui_height_ = 0;
};
}  // namespace ms
