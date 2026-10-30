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
#include "PanelRenderer.h"

#include <cmath>

#include "../Constants.h"

namespace ms {
void PanelRenderer::flush() {
    if (empty()) {
        return;
    }

    GraphicsGL &graphics = GraphicsGL::get();

    // The windows here are laid out on a screen PANEL_SCALE smaller than the scene, so a panel is
    // magnified that much more than the interface around it. Like the interface's own layout, this
    // comes from the scene the picture is actually being drawn at and not from the live resolution,
    // which is what keeps a held transition frame mapped the way it was drawn.
    const int16_t layout_width =
        static_cast<int16_t>(std::ceil(graphics.get_screen_width() / Constants::PANEL_SCALE));
    const int16_t layout_height =
        static_cast<int16_t>(std::ceil(graphics.get_screen_height() / Constants::PANEL_SCALE));

    // The target is built at the real surface size of the view box, so a panel is rasterised at
    // native pixel density at its own magnification rather than drawn small and stretched. Fall back
    // to the layout size only if a viewport update has not landed yet (first frame).
    const int16_t target_width =
        native_width_ > 0 ? static_cast<int16_t>(native_width_) : layout_width;
    const int16_t target_height =
        native_height_ > 0 ? static_cast<int16_t>(native_height_) : layout_height;

    if (panel_width_ != target_width || panel_height_ != target_height) {
        if (panel_fbo_) {
            glDeleteFramebuffers(1, &panel_fbo_);
            glDeleteTextures(1, &panel_tex_);
        }

        glGenTextures(1, &panel_tex_);
        glBindTexture(GL_TEXTURE_2D, panel_tex_);
        // The target matches the view box pixel-for-pixel, so end_scene() blits it one-to-one and
        // nothing is magnified after the fact. LINEAR stays on for the geometry magnification that
        // happens inside this pass (layout space -> native), which is what keeps a scaled sprite
        // smooth; the atlas itself is still nearest when sampled.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, target_width, target_height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);

        glGenFramebuffers(1, &panel_fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, panel_fbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               panel_tex_, 0);

        panel_width_ = target_width;
        panel_height_ = target_height;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, panel_fbo_);
    glViewport(0, 0, panel_width_, panel_height_);
    glScissor(0, 0, panel_width_, panel_height_);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // This target is composited as premultiplied alpha, so the accumulation inside it has to keep
    // rgb and alpha consistent: plain SRC_ALPHA blending would multiply the alpha channel by itself
    // (a*a instead of a), and the composite would then let the picture behind show through every
    // translucent panel far more than the art intends.
    glBlendFuncSeparate(GL_SRC_ALPHA,
                        GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE,
                        GL_ONE_MINUS_SRC_ALPHA);

    // Geometry is mapped from the layout size to the native target, which is the one rasterisation
    // that carries a window up to the size this pass shows it at. The lettering in the same batch is
    // already measured in the target's own pixels - at this pass's density, which is why its atlas is
    // its own - so flush_ui walks the batch once and switches the screensize between the runs.
    graphics.bind_atlas(graphics.panel_atlas_);
    graphics.flush_ui(panel_quads_, panel_circles_,
                      layout_width, layout_height, target_width, target_height);

    // Nothing else is expected to read this target while the frame is still open, so hand the scene
    // pass back the state it left.
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindFramebuffer(GL_FRAMEBUFFER, graphics.scene_fbo_);
    glViewport(0, 0, graphics.scene_width_, graphics.scene_height_);
    glScissor(0, 0, graphics.scene_width_, graphics.scene_height_);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    graphics.set_scene_attribs();
}
}  // namespace ms
