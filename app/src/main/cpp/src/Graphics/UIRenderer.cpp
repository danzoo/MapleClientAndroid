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
#include "UIRenderer.h"

#include <cmath>

#include "../Constants.h"

namespace ms {
void UIRenderer::flush() {
    if (empty()) {
        return;
    }

    GraphicsGL &graphics = GraphicsGL::get();

    // The interface is laid out on a screen UI_SCALE smaller than the scene. Taking that size from
    // the scene the picture is actually being drawn at, instead of from the live resolution, is
    // what keeps a held transition frame mapped the way it was drawn: reading the live size while
    // the scene is frozen squeezed the picture into the left of the view.
    const int16_t layout_width =
        static_cast<int16_t>(std::ceil(graphics.get_screen_width() / Constants::UI_SCALE));
    const int16_t layout_height =
        static_cast<int16_t>(std::ceil(graphics.get_screen_height() / Constants::UI_SCALE));

    // The target is built at the real surface size of the view box, so the interface is rasterised
    // at native pixel density instead of being drawn small and stretched. Fall back to the layout
    // size only if a viewport update has not landed yet (first frame).
    const int16_t target_width =
        native_width_ > 0 ? static_cast<int16_t>(native_width_) : layout_width;
    const int16_t target_height =
        native_height_ > 0 ? static_cast<int16_t>(native_height_) : layout_height;

    if (ui_width_ != target_width || ui_height_ != target_height) {
        if (ui_fbo_) {
            glDeleteFramebuffers(1, &ui_fbo_);
            glDeleteTextures(1, &ui_tex_);
        }

        glGenTextures(1, &ui_tex_);
        glBindTexture(GL_TEXTURE_2D, ui_tex_);
        // The target now matches the view box pixel-for-pixel, so end_scene() blits it one-to-one
        // and nothing is magnified after the fact. LINEAR stays on for the geometry magnification
        // that happens inside this pass (layout space -> native), which is what keeps a scaled
        // sprite smooth; the atlas itself is still nearest when sampled.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, target_width, target_height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);

        glGenFramebuffers(1, &ui_fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, ui_fbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               ui_tex_, 0);

        ui_width_ = target_width;
        ui_height_ = target_height;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, ui_fbo_);
    glViewport(0, 0, ui_width_, ui_height_);
    glScissor(0, 0, ui_width_, ui_height_);
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

    // Geometry is mapped from the layout size to the native target, which is what magnifies every
    // window, button and picture up to native pixel density in a single rasterisation, rather than
    // drawing small and stretching afterwards. The lettering in the same batch is already measured
    // in the target's own pixels, so flush_ui walks the batch once and switches the screensize
    // between the runs instead of drawing all the text last - doing that was what let the status
    // bar's name and the chat line float over windows opened after them.
    graphics.bind_atlas(graphics.ui_atlas_);
    graphics.flush_ui(ui_quads_, ui_circles_,
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
