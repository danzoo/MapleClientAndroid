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
#include "GraphicsGL.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <iterator>
#include <utility>
#include <vector>

#include <android/log.h>

#include "../Configuration.h"
#include "../Perf.h"
#include "AndroidText.h"
#include "PanelRenderer.h"
#include "UIRenderer.h"
#include "Window.h"

namespace ms {
    GraphicsGL::GraphicsGL()
        : locked_(false), worker_decode_us_(0), worker_decode_pixels_(0) {
        VWIDTH = Constants::Constants::get().get_viewwidth();
        VHEIGHT = Constants::Constants::get().get_viewheight();
        SCREEN = Rectangle<int16_t>(0, VWIDTH, 0, VHEIGHT);

        scene_fbo_ = 0;
        scene_tex_ = 0;
        scene_width_ = 0;
        scene_height_ = 0;

        decode_thread_ = std::thread(&GraphicsGL::decode_worker, this);
    }

    GraphicsGL::~GraphicsGL() {
        {
            std::lock_guard<std::mutex> lock(decode_mutex_);
            decode_stop_ = true;
        }

        decode_cv_.notify_one();

        if (decode_thread_.joinable()) {
            decode_thread_.join();
        }
    }

    Error GraphicsGL::init(GLFMDisplay *pApp) {
        // Set image to left top. Each axis is normalised by the view's own size, so whatever
        // resolution the game runs at still covers the whole viewport.
        // Frag color changes to bgra because color is incorrect on Android platform
        const char *vertexShaderSource =
                "#version 320 es\n"
                "precision highp float;"
                "in vec4 coord;"
                "in vec4 color;"
                "out vec2 texpos;"
                "out vec4 colormod;"
                "uniform vec2 screensize;"
                "uniform int yoffset;"

                "void main(void)"
                "{"
                "	float x = -1.0 + 2.0 * coord.x / screensize.x;"
                "	float y = 1.0 - 2.0 * (coord.y + float(yoffset)) / screensize.y;"
                "   gl_Position = vec4(x, y, 0.0, 1.0);"
                "	texpos = coord.zw;"
                "	colormod = color;"
                "}";

        const char *fragmentShaderSource =
                "#version 320 es\n"
                "precision highp float;"
                "in vec2 texpos;"
                "in vec4 colormod;"
                "out vec4 FragColor;"
                "uniform sampler2D texture;"
                "uniform vec2 atlassize;"
                "uniform int fontregion;"

                "void main(void)"
                "{"
                "	if (texpos.y == 0.0)"
                "	{"
                "		FragColor = colormod;"
                "	}"
                "	else if (texpos.y <= float(fontregion))"
                "	{"
                "		FragColor = vec4(1.0, 1.0, 1.0, texture(texture, texpos / "
                "atlassize).r) * colormod;"
                "	}"
                "	else"
                "	{"
                "		FragColor = texture(texture, texpos / atlassize).bgra * "
                "colormod;"
                "	}"
                "}";

        const GLsizei bufSize = 512;

        GLint success;
        GLchar infoLog[bufSize];

        std::cout << "Using OpenGL " << glGetString(GL_VERSION) << std::endl;

        // Build and compile our shader program
        // ------------------------------------

        // Vertex Shader
        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
        glCompileShader(vertexShader);

        // Check for shader compile errors
        glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);

        if (success != GL_TRUE) {
            glGetShaderInfoLog(vertexShader, bufSize, nullptr, infoLog);

            return Error(Error::Code::VERTEX_SHADER, infoLog);
        }

        // Fragment Shader
        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
        glCompileShader(fragmentShader);

        // Check for shader compile errors
        glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);

        if (success != GL_TRUE) {
            glGetShaderInfoLog(fragmentShader, bufSize, nullptr, infoLog);

            return Error(Error::Code::FRAGMENT_SHADER, infoLog);
        }

        // Link Shaders
        shader_program_ = glCreateProgram();
        glAttachShader(shader_program_, vertexShader);
        glAttachShader(shader_program_, fragmentShader);
        glLinkProgram(shader_program_);

        // Check for linking errors
        glGetProgramiv(shader_program_, GL_LINK_STATUS, &success);

        if (success != GL_TRUE) {
            glGetProgramInfoLog(shader_program_, bufSize, nullptr, infoLog);

            return Error(Error::Code::SHADER_PROGRAM_LINK, infoLog);
        }

        // Validate Program
        glValidateProgram(shader_program_);

        // Check for validation errors
        glGetProgramiv(shader_program_, GL_VALIDATE_STATUS, &success);

        if (success != GL_TRUE) {
            glGetProgramInfoLog(shader_program_, bufSize, nullptr, infoLog);

            return Error(Error::Code::SHADER_PROGRAM_VALID, infoLog);
        }

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        attribute_coord_ = glGetAttribLocation(shader_program_, "coord");
        attribute_color_ = glGetAttribLocation(shader_program_, "color");
        uniform_texture_ = glGetUniformLocation(shader_program_, "texture");
        uniform_atlas_size_ = glGetUniformLocation(shader_program_, "atlassize");
        uniform_screen_size_ = glGetUniformLocation(shader_program_, "screensize");
        uniform_yoffset = glGetUniformLocation(shader_program_, "yoffset");
        uniform_font_region_ = glGetUniformLocation(shader_program_, "fontregion");

        if (attribute_coord_ == -1 || attribute_color_ == -1
            || uniform_texture_ == -1 || uniform_atlas_size_ == -1
            || uniform_screen_size_ == -1 || uniform_yoffset == -1) {
            return Error::Code::SHADER_VARS;
        }

        // The scene is composited into an off-screen texture and only then stretched over the view
        // box, which is where this program applies the filter.
        const char *filterVertexSource =
                "#version 320 es\n"
                "precision highp float;"
                "in vec2 coord;"
                "out vec2 texpos;"

                "void main(void)"
                "{"
                "   gl_Position = vec4(coord, 0.0, 1.0);"
                "   texpos = coord * 0.5 + 0.5;"
                "}";

        const char *filterFragmentSource =
                "#version 320 es\n"
                "precision highp float;"
                "in vec2 texpos;"
                "out vec4 FragColor;"
                "uniform sampler2D scene;"

                "void main(void)"
                "{"
                "   FragColor = texture(scene, texpos);"
                "}";

        GLuint filterVertex = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(filterVertex, 1, &filterVertexSource, nullptr);
        glCompileShader(filterVertex);
        glGetShaderiv(filterVertex, GL_COMPILE_STATUS, &success);

        if (success != GL_TRUE) {
            glGetShaderInfoLog(filterVertex, bufSize, nullptr, infoLog);

            return Error(Error::Code::VERTEX_SHADER, infoLog);
        }

        GLuint filterFragment = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(filterFragment, 1, &filterFragmentSource, nullptr);
        glCompileShader(filterFragment);
        glGetShaderiv(filterFragment, GL_COMPILE_STATUS, &success);

        if (success != GL_TRUE) {
            glGetShaderInfoLog(filterFragment, bufSize, nullptr, infoLog);

            return Error(Error::Code::FRAGMENT_SHADER, infoLog);
        }

        filter_program_ = glCreateProgram();
        glAttachShader(filter_program_, filterVertex);
        glAttachShader(filter_program_, filterFragment);
        glLinkProgram(filter_program_);
        glGetProgramiv(filter_program_, GL_LINK_STATUS, &success);

        if (success != GL_TRUE) {
            glGetProgramInfoLog(filter_program_, bufSize, nullptr, infoLog);

            return Error(Error::Code::SHADER_PROGRAM_LINK, infoLog);
        }

        filter_attribute_coord_ = glGetAttribLocation(filter_program_, "coord");
        filter_uniform_scene_ = glGetUniformLocation(filter_program_, "scene");

        if (filter_attribute_coord_ == -1 || filter_uniform_scene_ == -1) {
            return Error::Code::SHADER_VARS;
        }

        // One unit quad already covers clip space, so the scene only needs stretching to the view.
        static const GLfloat FULLSCREEN[] = {
                -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f,
                -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f
        };

        glGenBuffers(1, &filter_vbo_);
        glBindBuffer(GL_ARRAY_BUFFER, filter_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(FULLSCREEN), FULLSCREEN, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        // Vertex Buffer Object
        glGenBuffers(1, &VBO_);

        // One atlas per pass. The scene keeps nearest sampling because its art is pixel work that
        // must not blur; the interface is laid out on a smaller screen and magnified, so it
        // interpolates instead of showing stair-steps. The panel pass magnifies further still and is
        // cut to the size of the handful of windows it holds rather than a whole screen's art.
        init_atlas(scene_atlas_, ATLASW, ATLASH, false);
        init_atlas(ui_atlas_, ATLASW, ATLASH, true);
        init_atlas(panel_atlas_, PANEL_ATLASW, PANEL_ATLASH, true);

        if (!AndroidText::init(pApp)) {
            __android_log_print(ANDROID_LOG_ERROR, "NativeText", "bridge init failed");
        }

        return Error::Code::NONE;
    }

    void GraphicsGL::init_atlas(AtlasState &atlas, GLshort width, GLshort height, bool linear) {
        if (atlas.texture == 0) {
            glGenTextures(1, &atlas.texture);
        }

        atlas.width = width;
        atlas.height = height;

        glBindTexture(GL_TEXTURE_2D, atlas.texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA,
                     width,
                     height,
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     nullptr);

        atlas.glyph_region_bottom = GLYPH_REGION_H;
        atlas.glyphs.clear();
        reset_glyph_region(atlas);
        clear_internal(atlas);
    }

    void GraphicsGL::clear_internal(AtlasState &atlas) {
        perf_count(PerfCounter::AtlasWipes, 1);

        // Sprites are packed from the first row under the glyph strip. It has to be strictly below
        // the fontregion the shader compares against, or the top sprite row would be sampled as a
        // glyph (white times its red channel) instead of as a picture.
        atlas.border = Point<GLshort>(0, static_cast<GLshort>(atlas.glyph_region_bottom + 1));
        atlas.y_range = Range<GLshort>();
        atlas.offsets.clear();
        // Cleared with the offsets, because a sprite the atlas no longer has can only be painted
        // again by being asked for again - and an id left in here says that already happened.
        atlas.requested.clear();

        // A sprite caught half-painted has lost the room it was given, so it starts over in the
        // emptied atlas instead of finishing its remaining rows into somebody else's rect.
        for (DecodeJob &waiting : decode_ready_) {
            if (waiting.atlas == &atlas) {
                waiting.reserved = false;
                waiting.painted = 0;
            }
        }
        atlas.leftovers = QuadTree<size_t, Leftover>(
                [](const Leftover &first, const Leftover &second) {
                    bool width_comparison = first.width() >= second.width();
                    bool height_comparison = first.height() >= second.height();

                    if (width_comparison && height_comparison) {
                        return QuadTree<size_t, Leftover>::Direction::RIGHT;
                    }
                    if (width_comparison) {
                        return QuadTree<size_t, Leftover>::Direction::DOWN;
                    }
                    if (height_comparison) {
                        return QuadTree<size_t, Leftover>::Direction::UP;
                    }
                    return QuadTree<size_t, Leftover>::Direction::LEFT;
                });
        atlas.rlid = 1;
        atlas.wasted = 0;
    }

    void GraphicsGL::reset_glyph_region(AtlasState &atlas) {
        atlas.glyph_x = 1;
        atlas.glyph_y = 1;
        atlas.glyph_row_h = 0;
    }

    void GraphicsGL::bind_atlas(AtlasState &atlas) {
        glUseProgram(shader_program_);
        glBindTexture(GL_TEXTURE_2D, atlas.texture);
        glUniform2f(uniform_atlas_size_, atlas.width, atlas.height);
        glUniform1i(uniform_font_region_, atlas.glyph_region_bottom);
    }

    GraphicsGL::AtlasState &GraphicsGL::active_atlas() {
        switch (active_batch()) {
            case Batch::PANEL:
                return panel_atlas_;
            case Batch::UI:
                return ui_atlas_;
            case Batch::SCENE:
            default:
                return scene_atlas_;
        }
    }

    void GraphicsGL::set_atlas_text_scale(AtlasState &atlas, float scale) {
        if (std::abs(scale - atlas.text_scale) <= 0.01f) {
            return;
        }

        // Rewind the strip because its density moved: every bitmap in it was rasterised for the old
        // size. Logical metrics survive, since they are the device numbers divided back out of the
        // density; only the pixels have to be redrawn.
        atlas.text_scale = scale;
        atlas.rasters.clear();
        atlas.glyphs.clear();
        reset_glyph_region(atlas);
    }

    void GraphicsGL::set_output_size(int32_t box_width, int32_t box_height) {
        if (box_width <= 0 || box_height <= 0 || VWIDTH <= 0) {
            return;
        }

        // A fade holds the previous picture and its batches and re-submits them every frame, so
        // rewinding a strip here would move glyphs out from under the frame being shown.
        if (locked_) {
            return;
        }

        // The scene goes into a texture the size of the view and is then stretched over the box; the
        // interface and the panel are each laid out on a screen of their own smaller than the view
        // and rasterised straight into a target the size of the box. Each pass's text has to be built
        // for its own stretch, or the letters get resampled on the way to the screen.
        const int16_t layout_width =
                static_cast<int16_t>(std::ceil(VWIDTH / Constants::UI_SCALE));
        const int16_t panel_layout_width =
                static_cast<int16_t>(std::ceil(VWIDTH / Constants::PANEL_SCALE));

        set_atlas_text_scale(scene_atlas_,
                             static_cast<float>(box_width) / static_cast<float>(VWIDTH));
        set_atlas_text_scale(ui_atlas_,
                             static_cast<float>(box_width) / static_cast<float>(layout_width));
        set_atlas_text_scale(panel_atlas_,
                             static_cast<float>(box_width) / static_cast<float>(panel_layout_width));
    }

    void GraphicsGL::font_metrics(Text::Font id, float &px, bool &bold) {
        switch (id) {
            case Text::Font::A11B:
                px = 11.0f;
                bold = true;
                break;
            case Text::Font::A12M:
                px = 12.0f;
                bold = false;
                break;
            case Text::Font::A12B:
                px = 12.0f;
                bold = true;
                break;
            case Text::Font::A13M:
                px = 13.0f;
                bold = false;
                break;
            case Text::Font::A13B:
                px = 13.0f;
                bold = true;
                break;
            case Text::Font::A15B:
                px = 15.0f;
                bold = true;
                break;
            case Text::Font::A18M:
                px = 18.0f;
                bold = false;
                break;
            case Text::Font::A11M:
            default:
                px = 11.0f;
                bold = false;
                break;
        }
    }

    int16_t GraphicsGL::font_linespace(Text::Font id) const {
        float px;
        bool bold;
        font_metrics(id, px, bold);

        return static_cast<int16_t>(px * 1.35 + 1);
    }

    int16_t GraphicsGL::glyph_advance(uint32_t codepoint, Text::Font id) {
        uint64_t key = (static_cast<uint64_t>(id) << 32) | codepoint;

        auto cached = advance_cache_.find(key);

        if (cached != advance_cache_.end()) {
            return cached->second;
        }

        float px;
        bool bold;
        font_metrics(id, px, bold);

        float advance = 0.0f;
        AndroidText::measure(codepoint, px, bold, advance);

        return advance_cache_.emplace(key,
                                      static_cast<int16_t>(std::lround(advance))).first->second;
    }

    const GraphicsGL::GlyphRaster &
    GraphicsGL::get_raster(uint32_t codepoint, Text::Font id, AtlasState &atlas) {
        uint64_t key = (static_cast<uint64_t>(id) << 32) | codepoint;

        auto cached = atlas.rasters.find(key);

        if (cached != atlas.rasters.end()) {
            return cached->second;
        }

        float px;
        bool bold;
        font_metrics(id, px, bold);

        const float density = atlas.text_scale;

        GlyphBitmap bmp;
        AndroidText::rasterize(codepoint, px * density, bold, bmp);

        GlyphRaster raster;

        // The advance is shared by every atlas so a layout counted in one pass still fits the glyph
        // drawn in the other. Blank glyphs (a space) have no ink but still move the pen.
        raster.ax = glyph_advance(codepoint, id);

        if (bmp.width > 0 && bmp.height > 0) {
            // The device box is the truth the glyph is drawn from: its bearings and size exactly as
            // the rasteriser produced them. The logical box is only ever used to measure a plate
            // around the ink, where a rounding difference of one pixel is invisible.
            raster.dx = static_cast<GLshort>(std::lround(bmp.bearing_x));
            raster.dy = static_cast<GLshort>(std::lround(bmp.bearing_y));
            raster.nw = static_cast<GLshort>(bmp.width);
            raster.nh = static_cast<GLshort>(bmp.height);
            raster.bl = static_cast<GLshort>(std::lround(bmp.bearing_x / density));
            raster.bt = static_cast<GLshort>(std::lround(bmp.bearing_y / density));
            raster.bw = static_cast<GLshort>(std::lround(static_cast<float>(bmp.width) / density));
            raster.bh = static_cast<GLshort>(std::lround(static_cast<float>(bmp.height) / density));
            raster.alpha = std::move(bmp.alpha);
        }

        return atlas.rasters.emplace(key, std::move(raster)).first->second;
    }

    const GraphicsGL::Glyph &GraphicsGL::get_glyph(uint32_t codepoint, Text::Font id) {
        AtlasState &atlas = active_atlas();
        const GlyphRaster &raster = get_raster(codepoint, id, atlas);
        uint64_t key = (static_cast<uint64_t>(id) << 32) | codepoint;

        auto cached = atlas.glyphs.find(key);

        if (cached != atlas.glyphs.end()) {
            return cached->second;
        }

        Glyph glyph;
        glyph.ax = raster.ax;
        glyph.bl = raster.bl;
        glyph.bt = raster.bt;
        glyph.bw = raster.bw;
        glyph.bh = raster.bh;
        glyph.dx = raster.dx;
        glyph.dy = raster.dy;
        glyph.nw = raster.nw;
        glyph.nh = raster.nh;

        if (raster.nw > 0 && raster.nh > 0) {
            const GLshort w = raster.nw;
            const GLshort h = raster.nh;

            // Every upload carries a frame of transparent texels around the bitmap, so the gutter the
            // shelf leaves between two glyphs is painted rather than left as whatever the driver put
            // there. A glyph that is misaligned by a fraction of a pixel can then only ever blend
            // with nothing, never with its neighbour.
            const GLshort fw = static_cast<GLshort>(w + 2);
            const GLshort fh = static_cast<GLshort>(h + 2);

            // Shelf-pack into this atlas's glyph strip. A glyph too wide for the rest of the row
            // starts a new row; running off the bottom throws the strip away and starts over, which
            // is cheap because the raster is already cached and only the upload is redone.
            if (atlas.glyph_x + fw + 1 > atlas.width) {
                atlas.glyph_y = static_cast<int16_t>(atlas.glyph_y + atlas.glyph_row_h + 1);
                atlas.glyph_x = 1;
                atlas.glyph_row_h = 0;
            }

            if (atlas.glyph_y + fh + 1 > atlas.glyph_region_bottom) {
                atlas.glyphs.clear();
                reset_glyph_region(atlas);
            }

            glyph_scratch_.assign(static_cast<size_t>(fw) * fh * 4, 0);
            uint8_t *dst = glyph_scratch_.data();

            for (GLshort row = 0; row < h; row++) {
                uint8_t *drow = dst + (static_cast<size_t>(row) + 1) * fw * 4;

                for (GLshort col = 0; col < w; col++) {
                    const uint8_t coverage = raster.alpha[static_cast<size_t>(row) * w + col];
                    uint8_t *texel = drow + (static_cast<size_t>(col) + 1) * 4;

                    // Only the coverage channel is ever read back, by the shader's text branch; the
                    // rest is filled the way a sprite texel would be so the atlas stays uniform.
                    texel[0] = coverage;
                    texel[1] = coverage;
                    texel[2] = coverage;
                    texel[3] = 255;
                }
            }

            glBindTexture(GL_TEXTURE_2D, atlas.texture);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(GL_TEXTURE_2D,
                            0,
                            atlas.glyph_x,
                            atlas.glyph_y,
                            fw,
                            fh,
                            GL_RGBA,
                            GL_UNSIGNED_BYTE,
                            dst);

            glyph.offset = Offset(static_cast<GLshort>(atlas.glyph_x + 1),
                                  static_cast<GLshort>(atlas.glyph_y + 1),
                                  w,
                                  h);

            atlas.glyph_x = static_cast<int16_t>(atlas.glyph_x + fw + 1);

            if (fh > atlas.glyph_row_h) {
                atlas.glyph_row_h = fh;
            }
        }

        return atlas.glyphs.emplace(key, glyph).first->second;
    }

    void GraphicsGL::reinit() {
        int32_t new_width = Constants::Constants::get().get_viewwidth();
        int32_t new_height = Constants::Constants::get().get_viewheight();

        if (VWIDTH != new_width || VHEIGHT != new_height) {
            VWIDTH = new_width;
            VHEIGHT = new_height;
            SCREEN = Rectangle<int16_t>(0, VWIDTH, 0, VHEIGHT);
        }

        glUseProgram(shader_program_);

        glUniform2f(uniform_screen_size_, VWIDTH, VHEIGHT);

        glBindBuffer(GL_ARRAY_BUFFER, VBO_);
        glVertexAttribPointer(attribute_coord_,
                              4,
                              GL_SHORT,
                              GL_FALSE,
                              sizeof(Quad::Vertex),
                              0);
        glVertexAttribPointer(attribute_color_,
                              4,
                              GL_FLOAT,
                              GL_FALSE,
                              sizeof(Quad::Vertex),
                              (const void *) offsetof(Quad::Vertex, color));

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        clear_internal(scene_atlas_);
        clear_internal(ui_atlas_);
        clear_internal(panel_atlas_);

        bind_atlas(scene_atlas_);
    }

    void GraphicsGL::clear() {
        for (AtlasState *atlas : {&scene_atlas_, &ui_atlas_, &panel_atlas_}) {
            size_t used = atlas->width * atlas->border.y() + atlas->border.x() * atlas->y_range.second();
            double usedpercent = static_cast<double>(used) / (atlas->width * atlas->height);

            // usedpercent is a 0..1 fraction, so the threshold has to be one too. Comparing it
            // against 80.0 kept this reclaim dead, which left the mid-frame overflow inside
            // get_offset as the only way to free the atlas, and that one invalidates the offsets
            // of quads already batched for the frame being drawn.
            if (usedpercent > 0.80) {
                clear_internal(*atlas);
            }
        }
    }

    void GraphicsGL::add_bitmap(const img::bitmap &bmp) {
        get_offset(scene_atlas_, bmp);
    }

    const GraphicsGL::Offset *GraphicsGL::get_offset(AtlasState &atlas, const img::bitmap &bmp) {
        const size_t id = bmp.id();
        auto offiter = atlas.offsets.find(id);

        if (offiter != atlas.offsets.end()) {
            return &offiter->second;
        }

        const GLshort width = bmp.width();
        const GLshort height = bmp.height();

        if (width <= 0 || height <= 0) {
            return &null_offset_;
        }

        // A canvas nobody has painted yet is not painted here. Inflating one costs about as much as
        // drawing everything else in the frame put together, which is what the 45ms peaks in the
        // uidraw timings were: a screenful of art meeting the render thread for the first time. So
        // the pixels are asked for and the sprite is left out of this frame; it arrives as soon as
        // the worker has it and the frame's upload budget reaches it.
        request_decode(atlas, bmp);

        return nullptr;
    }

    bool GraphicsGL::request_decode(AtlasState &atlas, const img::bitmap &bmp) {
        if (atlas.requested.count(bmp.id())) {
            // Already asked for, and still on its way: queued, being inflated, or waiting behind the
            // frame's upload budget. Asking again would send a second copy of the same picture
            // through the packer, and every copy reserves fresh space and gets uploaded again, so a
            // screenful of art meeting an empty atlas filled it with duplicates until it wiped on
            // itself - which drops the offsets of the quads already batched for that frame, and is
            // what the flicker on entering a map was.
            return false;
        }

        if (decode_outstanding_ >= Constants::ATLAS_DECODE_LIMIT) {
            // Nothing is remembered about a sprite that could not be queued, so the frame that draws
            // it again asks for it again. This is the only reason a picture can stay missing for more
            // than a frame or two, and it means the worker is behind by more than the queue holds.
            return false;
        }

        DecodeJob job;
        job.bmp = bmp;
        job.atlas = &atlas;

        {
            std::lock_guard<std::mutex> lock(decode_mutex_);
            decode_queued_.push_back(std::move(job));
        }

        decode_outstanding_++;
        atlas.requested.insert(bmp.id());
        decode_cv_.notify_one();

        return true;
    }

    void GraphicsGL::decode_worker() {
        std::unique_lock<std::mutex> lock(decode_mutex_);

        while (true) {
            decode_cv_.wait(lock, [this] {
                return decode_stop_ || !decode_queued_.empty();
            });

            if (decode_stop_) {
                return;
            }

            DecodeJob job = std::move(decode_queued_.front());
            decode_queued_.pop_front();

            // The pixels are allocated and written with no lock held, which is the whole point: the
            // inflate and the buffer both happen while the render thread is drawing the frame this
            // one cannot be in.
            lock.unlock();

            job.pixels.resize(job.bmp.length());

            const auto began = std::chrono::steady_clock::now();
            job.decoded
                    = job.bmp.decode_into(job.pixels.data(), job.pixels.size()) == job.pixels.size();
            worker_decode_us_.fetch_add(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - began).count(),
                    std::memory_order_relaxed);
            worker_decode_pixels_.fetch_add(job.pixels.size(), std::memory_order_relaxed);

            lock.lock();
            decode_done_.push_back(std::move(job));
            decode_done_cv_.notify_all();
        }
    }

    void GraphicsGL::move_completed() {
        size_t finished = 0;

        {
            std::lock_guard<std::mutex> lock(decode_mutex_);
            finished = decode_done_.size();
            decode_ready_.insert(decode_ready_.end(),
                                 std::make_move_iterator(decode_done_.begin()),
                                 std::make_move_iterator(decode_done_.end()));
            decode_done_.clear();
        }

        decode_outstanding_ -= finished;

        perf_count(PerfCounter::WorkerDecodeUs, worker_decode_us_.exchange(0));
        perf_count(PerfCounter::WorkerDecodePixels, worker_decode_pixels_.exchange(0));
    }

    void GraphicsGL::pump_uploads() {
        PerfScope perf(PerfSection::AtlasPump);

        // The budget is there to stop a frame that is being watched from spending all of its time
        // handing pixels to the driver. A catch-up pass is not that frame: the screen it is filling in
        // is not on show (Window::end holds the last complete one), so the only thing a shorter pass
        // would buy is more frames of the same screen held up.
        const bool catch_up = catch_up_;
        const int64_t budget_us = catch_up
                ? Constants::ATLAS_PUMP_CATCHUP_US
                : Constants::ATLAS_PUMP_BUDGET_US;

        const auto began = std::chrono::steady_clock::now();

        while (true) {
            move_completed();

            if (decode_ready_.empty()) {
                const int64_t spent = std::chrono::duration_cast<std::chrono::microseconds>(
                                              std::chrono::steady_clock::now() - began).count();

                if (!catch_up || spent > budget_us) {
                    break;
                }

                if (decode_outstanding_ == 0) {
                    // Nothing is coming, so this is as filled in as the screen gets.
                    catch_up_ = false;
                    break;
                }

                // The worker is inflating what the held screen is waiting on. Sleeping on it is the
                // point: taking the answer one polled frame at a time is what left a screen blank for
                // seconds after a map change.
                std::unique_lock<std::mutex> lock(decode_mutex_);
                decode_done_cv_.wait_for(
                        lock,
                        std::chrono::microseconds(budget_us - spent),
                        [this] { return decode_stop_ || !decode_done_.empty(); });
                continue;
            }

            DecodeJob &job = decode_ready_.front();

            if (!job.decoded) {
                // A payload that does not inflate stays unpainted, and is not asked for again until
                // the atlas is repacked. Caching the blank offset is what keeps a broken canvas from
                // holding a queue slot on every frame.
                job.atlas->offsets.emplace(job.bmp.id(), null_offset_);
                decode_ready_.pop_front();
                continue;
            }

            paint_job(job, Constants::ATLAS_UPLOAD_SLICE_BYTES);

            if (job.painted >= job.ph) {
                decode_ready_.pop_front();
            }

            // Checked after every slice rather than after every sprite, because a background sheet
            // takes several of them and the frame is not going to wait for one to finish before it
            // stops paying. Nothing in the queue is half-painted except the front of it - the rect
            // was reserved whole - so leaving here is enough to keep the order.
            const auto spent = std::chrono::duration_cast<std::chrono::microseconds>(
                                   std::chrono::steady_clock::now() - began).count();

            if (spent > budget_us) {
                break;
            }
        }

        perf_count(PerfCounter::AtlasPending, static_cast<int64_t>(decode_ready_.size()));
    }

    bool GraphicsGL::hold_frame() {
        const bool holes = frame_holes_;
        frame_holes_ = false;

        // Holding is only worth anything while something is actually on its way, because a held frame
        // never reaches the swap and the swap is what paces this loop. A hole whose pixels were turned
        // away at the queue cap, or one whose canvas failed to inflate, is a hole that waiting will
        // not fill: show it and let the frame rate come back.
        const bool pending = !decode_ready_.empty() || decode_outstanding_ > 0;

        if (!holes || !pending || held_frames_ >= Constants::ATLAS_HOLD_FRAME_LIMIT) {
            held_frames_ = 0;
            return false;
        }

        held_frames_++;
        perf_count(PerfCounter::AtlasHeld, 1);
        return true;
    }

    void GraphicsGL::reserve_space(AtlasState &atlas, DecodeJob &job) {
        const img::bitmap &bmp = job.bmp;

        const GLshort width = bmp.width();
        const GLshort height = bmp.height();

        // Bitmaps are stored with a one-texel border of replicated edge pixels. Texture
        // coordinates address texel corners, so a filtered sample taken exactly on a sprite's
        // edge would otherwise blend with whichever sprite happens to be packed next to it in
        // the atlas; the replicated border makes those edge samples resolve to the sprite's
        // own edge colour. This is what keeps a stretched one-pixel fill (tooltip bodies) and
        // abutting nine-slice pieces (window borders) free of foreign-colour fringes.
        job.pw = static_cast<GLshort>(width + 2);
        job.ph = static_cast<GLshort>(height + 2);

        GLshort x = 0;
        GLshort y = 0;

        Leftover value = Leftover(x, y, job.pw, job.ph);

        size_t lid = atlas.leftovers.findnode(
                value,
                [](const Leftover &val, const Leftover &leaf) {
                    return val.width() <= leaf.width() && val.height() <= leaf.height();
                });

        if (lid > 0) {
            const Leftover &leftover = atlas.leftovers[lid];

            x = leftover.left;
            y = leftover.top;

            GLshort width_delta = leftover.width() - job.pw;
            GLshort height_delta = leftover.height() - job.ph;

            atlas.leftovers.erase(lid);

            atlas.wasted -= job.pw * job.ph;

            if (width_delta >= MINLOSIZE && height_delta >= MINLOSIZE) {
                atlas.leftovers.add(
                        atlas.rlid,
                        Leftover(x + job.pw, y + job.ph, width_delta, height_delta));
                atlas.rlid++;

                if (job.pw >= MINLOSIZE) {
                    atlas.leftovers.add(atlas.rlid,
                                        Leftover(x, y + job.ph, job.pw, height_delta));
                    atlas.rlid++;
                }

                if (job.ph >= MINLOSIZE) {
                    atlas.leftovers.add(atlas.rlid,
                                        Leftover(x + job.pw, y, width_delta, job.ph));
                    atlas.rlid++;
                }
            } else if (width_delta >= MINLOSIZE) {
                atlas.leftovers.add(
                        atlas.rlid,
                        Leftover(x + job.pw, y, width_delta, job.ph + height_delta));
                atlas.rlid++;
            } else if (height_delta >= MINLOSIZE) {
                atlas.leftovers.add(
                        atlas.rlid,
                        Leftover(x, y + job.ph, job.pw + width_delta, height_delta));
                atlas.rlid++;
            }
        } else {
            if (atlas.border.x() + job.pw > atlas.width) {
                atlas.border.set_x(0);
                atlas.border.shift_y(atlas.y_range.second());

                if (atlas.border.y() + job.ph > atlas.height) {
                    clear_internal(atlas);
                } else {
                    atlas.y_range = Range<GLshort>();
                }
            }

            x = atlas.border.x();
            y = atlas.border.y();

            atlas.border.shift_x(job.pw);

            if (job.ph > atlas.y_range.second()) {
                if (x >= MINLOSIZE && job.ph - atlas.y_range.second() >= MINLOSIZE) {
                    atlas.leftovers.add(atlas.rlid,
                                        Leftover(0,
                                                 atlas.y_range.first(),
                                                 x,
                                                 job.ph - atlas.y_range.second()));
                    atlas.rlid++;
                }

                atlas.wasted += x * (job.ph - atlas.y_range.second());

                atlas.y_range = Range<int16_t>(y + job.ph, job.ph);
            } else if (job.ph < atlas.y_range.first() - y) {
                if (job.pw >= MINLOSIZE
                    && atlas.y_range.first() - y - job.ph >= MINLOSIZE) {
                    atlas.leftovers.add(atlas.rlid,
                                        Leftover(x,
                                                 y + job.ph,
                                                 job.pw,
                                                 atlas.y_range.first() - y - job.ph));
                    atlas.rlid++;
                }

                atlas.wasted += job.pw * (atlas.y_range.first() - y - job.ph);
            }
        }

        job.x = x;
        job.y = y;
    }

    void GraphicsGL::paint_job(DecodeJob &job, size_t slice) {
        AtlasState &atlas = *job.atlas;

        if (!job.reserved) {
            reserve_space(atlas, job);
            job.reserved = true;

            // Making room can mean wiping the atlas, and a wipe empties the list of what has been
            // asked for. This one is on its way whatever the packer had to do to fit it, so put it
            // back on the list.
            atlas.requested.insert(job.bmp.id());
        }

        const size_t row_bytes = static_cast<size_t>(job.pw) * 4;
        const size_t left = static_cast<size_t>(job.ph - job.painted);

        // The rows one slice covers, of the rows this sprite still has waiting. At least one, even for
        // a sprite wider than the whole slice, so that no canvas is ever too wide to make progress.
        size_t room = slice / row_bytes;
        if (room < 1) {
            room = 1;
        }
        if (room > left) {
            room = left;
        }

        const GLshort rows = static_cast<GLshort>(room);

        // The border is built in one reusable scratch buffer, one slice at a time. A per-bitmap vector
        // would have to be mmapped, zeroed and munmapped again for every sprite, and the login
        // animation frames are large enough for that to dominate the time spent painting the logo.
        atlas_scratch_.resize(row_bytes * room);
        uint8_t *dst = atlas_scratch_.data();

        const uint8_t *src = job.pixels.data();
        const GLshort width = job.bmp.width();
        const GLshort height = job.bmp.height();
        const size_t src_stride = static_cast<size_t>(width) * 4;

        for (GLshort row = job.painted; row < job.painted + rows; row++) {
            // Row 0 repeats the first source row and row ph-1 the last; the outer columns repeat
            // the first and last pixel of their row.
            const GLshort srow = std::min<GLshort>(std::max<GLshort>(row - 1, 0), height - 1);
            const uint8_t *srowp = src + static_cast<size_t>(srow) * src_stride;
            uint8_t *drowp = dst + static_cast<size_t>(row - job.painted) * row_bytes;

            std::memcpy(drowp + 4, srowp, src_stride);
            std::memcpy(drowp, srowp, 4);
            std::memcpy(drowp + 4 + src_stride, srowp + src_stride - 4, 4);
        }

        glBindTexture(GL_TEXTURE_2D, atlas.texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D,
                        0,
                        job.x,
                        job.y + job.painted,
                        job.pw,
                        rows,
                        GL_RGBA,
                        GL_UNSIGNED_BYTE,
                        dst);

        job.painted = static_cast<GLshort>(job.painted + rows);

        if (job.painted < job.ph) {
            return;
        }

        perf_count(PerfCounter::AtlasUploads, 1);
        perf_count(PerfCounter::AtlasBytes, static_cast<int64_t>(job.pw) * job.ph * 4);

        // Sampled here because this is the only moment the packing state is looked at between the
        // ones that matter: how full the atlas is decides how soon the next wipe comes, and a wipe
        // is what makes every sprite in the window get decoded and uploaded again.
        const size_t used = static_cast<size_t>(atlas.width) * atlas.border.y()
                + static_cast<size_t>(atlas.border.x()) * atlas.y_range.second();
        perf_count(PerfCounter::AtlasOccupancy,
                   static_cast<int64_t>(used * 1000 / (atlas.width * atlas.height)));

        // Only now is the sprite something the frame can look up and draw.
        atlas.offsets
                .emplace(std::piecewise_construct,
                         std::forward_as_tuple(job.bmp.id()),
                         std::forward_as_tuple(static_cast<GLshort>(job.x + 1),
                                               static_cast<GLshort>(job.y + 1),
                                               width,
                                               height));
    }

    void GraphicsGL::draw(const img::bitmap &bmp,
                          const Rectangle<int16_t> &rect,
                          const Color &color,
                          float angle) {
        if (locked_) {
            return;
        }

        if (color.invisible()) {
            return;
        }

        if (!rect.overlaps(SCREEN)) {
            return;
        }

        const Offset *offset = get_offset(active_atlas(), bmp);

        if (offset == nullptr) {
            // The pixels are still being inflated, and there is nothing in the atlas to sample for
            // this rect yet. Leaving the quad out is the only way to be honest about that: a blank
            // offset would fill the rect with whatever colour the shader gives an untextured quad,
            // which is how a loading picture would announce itself as a solid block of its own tint.
            //
            // A hole is a hole however it is left, so the frame that has one is asked not to be shown
            // (hold_frame) and the next pump is told to finish the job without counting the cost.
            frame_holes_ = true;
            catch_up_ = true;
            perf_count(PerfCounter::AtlasSkips, 1);
            return;
        }

        batch_quads().emplace_back(rect.left(),
                                   rect.right(),
                                   rect.top(),
                                   rect.bottom(),
                                   *offset,
                                   color,
                                   angle);
    }

// Text::Layout GraphicsGL::create_layout(const std::string &text,
//                                        Text::Font id,
//                                        Text::Alignment alignment,
//                                        int16_t maxwidth,
//                                        bool formatted,
//                                        int16_t line_adj) {
//     size_t length = text.length();

//     if (length == 0) {
//         return Text::Layout();
//     }

//     LayoutBuilder builder(fonts_[id], alignment, maxwidth, formatted,
//     line_adj);

//     const char *p_text = text.c_str();

//     size_t first = 0;
//     size_t offset = 0;

//     while (offset < length) {
//         size_t last = text.find_first_of(" \\#", offset + 1);

//         if (last == std::string::npos) {
//             last = length;
//         }

//         first = builder.add(p_text, first, offset, last);
//         offset = last;
//     }

//     return builder.finish(first, offset);
// }

    Text::Layout GraphicsGL::create_layout(
            const std::vector<uint32_t> &text_as_values,
            Text::Font id,
            Text::Alignment alignment,
            int16_t maxwidth,
            bool formatted,
            int16_t line_adj) {
        size_t length = text_as_values.size();

        if (length == 0) {
            return Text::Layout();
        }

        LayoutBuilder builder(*this, id, alignment, maxwidth, formatted, line_adj);

        const auto *p_text = text_as_values.data();

        size_t first = 0;
        size_t offset = 0;

        std::vector<uint32_t> to_find = {' ', '\\', '#'};

        while (offset < length) {
            auto it = std::find_first_of(text_as_values.begin() + offset + 1,
                                         text_as_values.end(),
                                         to_find.begin(),
                                         to_find.end());
            size_t last = it - text_as_values.begin();

            if (it == text_as_values.end()) {
                last = length;
            }

            first = builder.add(p_text, first, offset, last);
            offset = last;
        }

        return builder.finish(first, offset);
    }

    size_t GraphicsGL::LayoutBuilder::add(const unsigned int *text,
                                          size_t prev,
                                          size_t first,
                                          size_t last) {
        if (first == last) {
            return prev;
        }

        Text::Font last_font = font_id_;
        Color::Name last_color = color_;
        size_t skip = 0;
        bool linebreak = false;

        if (formatted_) {
            switch (text[first]) {
                case '\\':
                    if (first + 1 < last) {
                        switch (text[first + 1]) {
                            case 'n':
                                linebreak = true;
                                break;
                            case 'r':
                                linebreak = ax_ > 0;
                                break;
                        }

                        skip++;
                    }

                    skip++;
                    break;
                case '#':
                    if (first + 1 < last) {
                        switch (text[first + 1]) {
                            case 'k':
                                color_ = Color::Name::DARKGREY;
                                skip++;
                                break;
                            case 'b':
                                color_ = Color::Name::BLUE;
                                skip++;
                                break;
                            case 'r':
                                color_ = Color::Name::RED;
                                skip++;
                                break;
                            case 'c':
                                color_ = Color::Name::ORANGE;
                                skip++;
                                break;
                            case 'L':
                                skip++;
                                break;
                            case 'l':
                                skip++;
                                break;
                            default:
                                break;
                        }
                    }

                    skip++;
                    break;
            }
        }

        int16_t wordwidth = 0;

        if (!linebreak) {
            for (size_t i = first; i < last; i++) {
                auto c = text[i];

                if (c < 32) {
                    if (c == 10) { // 10 = line feed
                        linebreak = true;
                    } else if (c == 13) { // 13 = carriage return
                        linebreak = ax_ > 0;
                    }

                    continue;
                }

                wordwidth += graphics_.glyph_advance(c, id_);

                if (wordwidth > max_width_) {
                    if (last - first == 1) {
                        return last;
                    }

                    prev = add(text, prev, first, i);
                    return add(text, prev, i, last);
                }
            }
        }

        bool newword = skip > 0;
        bool newline = linebreak || ax_ + wordwidth > max_width_;

        if (newword || newline) {
            add_word(prev, first, last_font, last_color);
        }

        if (newline) {
            add_line();

            endy_ = ay_;
            ax_ = 0;
            ay_ += graphics_.font_linespace(id_);

            if (!lines_.empty()) {
                ay_ -= line_adj_;
            }
        }

        for (size_t pos = first; pos < last; pos++) {
            auto c = text[pos];
            const int16_t advance = graphics_.glyph_advance(c, id_);

            advances_.push_back(ax_);

            if (pos < first + skip || newline && c == ' ') {
                continue;
            }

            if (c < 32) {
                continue;
            }

            ax_ += advance;

            if (width_ < ax_) {
                width_ = ax_;
            }
        }

        if (newword || newline) {
            return first + skip;
        }

        return prev;
    }

    GraphicsGL::LayoutBuilder::LayoutBuilder(GraphicsGL &graphics,
                                             Text::Font id,
                                             Text::Alignment a,
                                             int16_t mw,
                                             bool fm,
                                             int16_t la) :
            graphics_(graphics),
            id_(id),
            alignment_(a),
            max_width_(mw),
            formatted_(fm),
            line_adj_(la),
            font_id_(Text::Font::NUM_FONTS),
            color_(Color::Name::NUM_COLORS),
            ax_(0),
            ay_(graphics.font_linespace(id)),
            width_(0),
            endy_(0) {
        if (max_width_ == 0) {
            max_width_ = 800;
        }
    }

// size_t GraphicsGL::LayoutBuilder::add(const char *text,
//                                       size_t prev,
//                                       size_t first,
//                                       size_t last) {
//     if (first == last) {
//         return prev;
//     }

//     Text::Font last_font = font_id_;
//     Color::Name last_color = color_;
//     size_t skip = 0;
//     bool linebreak = false;

//     if (formatted_) {
//         switch (text[first]) {
//             case '\\':
//                 if (first + 1 < last) {
//                     switch (text[first + 1]) {
//                         case 'n': linebreak = true; break;
//                         case 'r': linebreak = ax_ > 0; break;
//                     }

//                     skip++;
//                 }

//                 skip++;
//                 break;
//             case '#':
//                 if (first + 1 < last) {
//                     switch (text[first + 1]) {
//                         case 'k': color_ = Color::Name::DARKGREY; break;
//                         case 'b': color_ = Color::Name::BLUE; break;
//                         case 'r': color_ = Color::Name::RED; break;
//                         case 'c': color_ = Color::Name::ORANGE; break;
//                     }

//                     skip++;
//                 }

//                 skip++;
//                 break;
//         }
//     }

//     int16_t wordwidth = 0;

//     if (!linebreak) {
//         for (size_t i = first; i < last; i++) {
//             auto c = text[i];
//             wordwidth += font_.chars[c].ax;

//             if (wordwidth > max_width_) {
//                 if (last - first == 1) {
//                     return last;
//                 }

//                 prev = add(text, prev, first, i);
//                 return add(text, prev, i, last);
//             }
//         }
//     }

//     bool newword = skip > 0;
//     bool newline = linebreak || ax_ + wordwidth > max_width_;

//     if (newword || newline) {
//         add_word(prev, first, last_font, last_color);
//     }

//     if (newline) {
//         add_line();

//         endy_ = ay_;
//         ax_ = 0;
//         ay_ += font_.linespace();

//         if (!lines_.empty()) {
//             ay_ -= line_adj_;
//         }
//     }

//     for (size_t pos = first; pos < last; pos++) {
//         auto c = text[pos];
//         const Font::Char &ch = font_.chars[c];

//         advances_.push_back(ax_);

//         if (pos < first + skip || newline && c == ' ') {
//             continue;
//         }

//         ax_ += ch.ax;

//         if (width_ < ax_) {
//             width_ = ax_;
//         }
//     }

//     if (newword || newline) {
//         return first + skip;
//     }

//     return prev;
// }

    Text::Layout GraphicsGL::LayoutBuilder::finish(size_t first, size_t last) {
        add_word(first, last, font_id_, color_);
        add_line();

        advances_.push_back(ax_);

        return Text::Layout(lines_, advances_, width_, ay_, ax_, endy_);
    }

    void GraphicsGL::LayoutBuilder::add_word(size_t word_first,
                                             size_t word_last,
                                             Text::Font word_font,
                                             Color::Name word_color) {
        words_.push_back({word_first, word_last, word_font, word_color});
    }

    void GraphicsGL::LayoutBuilder::add_line() {
        int16_t line_x = 0;
        int16_t line_y = ay_;

        switch (alignment_) {
            case Text::Alignment::CENTER:
                line_x -= ax_ / 2;
                break;
            case Text::Alignment::RIGHT:
                line_x -= ax_;
                break;
        }

        lines_.push_back({words_, {line_x, line_y}});
        words_.clear();
    }

// void GraphicsGL::draw_text(const DrawArgument &args,
//                            const Range<int16_t> &vertical,
//                            const std::string &text,
//                            const Text::Layout &layout,
//                            Text::Font id,
//                            Color::Name colorid,
//                            Text::Background background) {
//     if (locked_) {
//         return;
//     }

//     const Color &color = args.get_color();

//     if (text.empty() || color.invisible()) {
//         return;
//     }

//     auto vec = to_utf8_vector(text);

//     const Font &font = fonts_[id];

//     GLshort x = args.get_pos().x();
//     GLshort y = args.get_pos().y();
//     GLshort w = layout.width();
//     GLshort h = layout.height();
//     GLshort minheight = vertical.first() > 0 ? vertical.first() :
//     SCREEN.top(); GLshort maxheight =
//         vertical.second() > 0 ? vertical.second() : SCREEN.bottom();

//     switch (background) {
//         case Text::Background::NAMETAG:
//             // If ever changing code in here confirm placements with map
//             10000 for (const Text::Layout::Line &line : layout) {
//                 GLshort left = x + line.position.x() - 1;
//                 GLshort right = left + w + 3;
//                 GLshort top = y + line.position.y() - font.linespace() + 6;
//                 GLshort bottom = top + h - 2;
//                 Color ntcolor = Color(0.0f, 0.0f, 0.0f, 0.6f);

//                 quads_.emplace_back(left,
//                                     right,
//                                     top,
//                                     bottom,
//                                     null_offset_,
//                                     ntcolor,
//                                     0.0f);
//                 quads_.emplace_back(left - 1,
//                                     left,
//                                     top + 1,
//                                     bottom - 1,
//                                     null_offset_,
//                                     ntcolor,
//                                     0.0f);
//                 quads_.emplace_back(right,
//                                     right + 1,
//                                     top + 1,
//                                     bottom - 1,
//                                     null_offset_,
//                                     ntcolor,
//                                     0.0f);
//             }

//             break;
//     }

//     for (const Text::Layout::Line &line : layout) {
//         Point<int16_t> position = line.position;

//         for (const Text::Layout::Word &word : line.words) {
//             GLshort ax = position.x() + layout.advance(word.first);
//             GLshort ay = position.y();

//             const GLfloat *wordcolor = nullptr;

//             if (word.color < Color::Name::NUM_COLORS) {
//                 wordcolor = Color::colors[word.color];
//             } else {
//                 wordcolor = Color::colors[colorid];
//             }

//             Color abscolor =
//                 color * Color(wordcolor[0], wordcolor[1],
//                 wordcolor[2], 1.0f);

//             for (size_t pos = word.first; pos < word.last; ++pos) {
//                 auto c = static_cast<unsigned char>(text[pos]);
//                 const Font::Char &ch = font.chars[c];

//                 GLshort char_x = x + ax + ch.bl;
//                 GLshort char_y = y + ay - ch.bt;
//                 GLshort char_width = ch.bw;
//                 GLshort char_height = ch.bh;
//                 GLshort char_bottom = char_y + char_height;

//                 Offset offset = ch.offset;

//                 if (char_bottom > maxheight) {
//                     GLshort bottom_adjust = char_bottom - maxheight;

//                     if (bottom_adjust < 10) {
//                         offset.bottom -= bottom_adjust;
//                         char_bottom -= bottom_adjust;
//                     } else {
//                         continue;
//                     }
//                 }

//                 if (char_y < minheight) {
//                     continue;
//                 }

//                 if (ax == 0 && c == ' ') {
//                     continue;
//                 }

//                 ax += ch.ax;

//                 if (char_width <= 0 || char_height <= 0) {
//                     continue;
//                 }

//                 quads_.emplace_back(char_x,
//                                     char_x + char_width,
//                                     char_y,
//                                     char_bottom,
//                                     offset,
//                                     abscolor,
//                                     0.0f);
//             }
//         }
//     }
// }

    Text::VerticalInk GraphicsGL::measure_ink_vertical(const std::vector<uint32_t> &text,
                                                       const Text::Layout &layout,
                                                       Text::Font id) {
        Text::VerticalInk ink{0, 0};
        bool found = false;

        for (const Text::Layout::Line &line: layout) {
            // The layout stores each line's baseline offset from the draw origin.
            const int16_t baseline = line.position.y();

            for (const Text::Layout::Word &word: line.words) {
                for (size_t pos = word.first; pos < word.last; ++pos) {
                    const Glyph &ch = get_glyph(text[pos], id);

                    if (ch.bw <= 0 || ch.bh <= 0) {
                        continue;
                    }

                    const int16_t glyph_top = baseline - ch.bt;
                    const int16_t glyph_bottom = glyph_top + ch.bh;

                    if (found) {
                        ink.top = std::min(ink.top, glyph_top);
                        ink.bottom = std::max(ink.bottom, glyph_bottom);
                    } else {
                        ink.top = glyph_top;
                        ink.bottom = glyph_bottom;
                        found = true;
                    }
                }
            }
        }

        return ink;
    }

    void GraphicsGL::draw_text(const DrawArgument &args,
                               const Range<int16_t> &vertical,
                               const std::vector<uint32_t> &text,
                               const Text::Layout &layout,
                               Text::Font id,
                               Color::Name colorid,
                               Text::Background background) {
        if (locked_) {
            return;
        }

        const Color &color = args.get_color();

        if (text.empty() || color.invisible()) {
            return;
        }

        GLshort x = args.get_pos().x();
        GLshort y = args.get_pos().y();
        GLshort w = layout.width();
        GLshort h = layout.height();
        GLshort minheight = vertical.first() > 0 ? vertical.first() : SCREEN.top();
        GLshort maxheight =
                vertical.second() > 0 ? vertical.second() : SCREEN.bottom();

        // Text is submitted in the device pixels of the target it is composited onto, at the density
        // the glyphs were rasterised for. Everything else in the same pass is still in logical pixels
        // and gets magnified, but a letter that goes through that magnification arrives as a blurred
        // average of itself - and, because the squeeze is not the same at every position, the strokes
        // of one word come out a shade heavier than the strokes of the next.
        const float density = active_atlas().text_scale;

        auto to_device = [density](int32_t logical) {
            return static_cast<GLshort>(std::lround(logical * density));
        };

        const GLshort dev_min = to_device(minheight);
        const GLshort dev_max = to_device(maxheight);
        // The old limit was ten logical pixels of overhang before a glyph was dropped outright; in
        // device pixels it is that many times the density.
        const int32_t max_trim = std::lround(10 * density);

        // Interface lettering is queued with the rest of the interface, in the order the windows drew
        // it, and only flagged, so that a window opened later still covers an earlier one's text.
        auto emit_text = [this](GLshort left, GLshort right, GLshort top, GLshort bottom,
                                const Offset &offset, const Color &color) {
            std::vector<Quad> &batch = batch_text_quads();

            batch.emplace_back(left, right, top, bottom, offset, color, 0.0f);
            batch.back().text = true;
        };

        switch (background) {
            case Text::Background::NAMETAG: {
                // If ever changing code in here confirm placements with map 10000
                // The plate has to follow the real ink box. Deriving it from the font's synthetic
                // line spacing only happened to look centred for the bundled latin faces, whose ink
                // reaches as high as the x-height; a CJK glyph's tight box fills nearly the whole
                // ascent and then sits flush against the top edge with the spare band below it.
                // Horizontally the advance box has the same problem: alignment centres it around the
                // draw position, but a glyph's left side bearing is not its right one, so the ink
                // sits off-centre inside it and the plate drifts away from the text.
                const Text::VerticalInk ink = measure_ink_vertical(text, layout, id);
                int16_t ink_top = y + ink.top;
                int16_t ink_bottom = y + ink.bottom;

                static constexpr int16_t NAMETAG_PAD = 2;

                if (ink.top == ink.bottom) {
                    // Nothing to measure - a run of blanks still gets the old band.
                    ink_top = y + 6;
                    ink_bottom = ink_top + h - 2;
                } else {
                    ink_top -= NAMETAG_PAD;
                    ink_bottom += NAMETAG_PAD;
                }

                for (const Text::Layout::Line &line: layout) {
                    int16_t ink_left = 0;
                    int16_t ink_right = 0;
                    bool found = false;

                    for (const Text::Layout::Word &word: line.words) {
                        for (size_t pos = word.first; pos < word.last; ++pos) {
                            const Glyph &ch = get_glyph(text[pos], id);

                            if (ch.bw <= 0 || ch.bh <= 0) {
                                continue;
                            }

                            const int16_t glyph_left = x + line.position.x()
                                                       + layout.advance(pos) + ch.bl;
                            const int16_t glyph_right = glyph_left + ch.bw;

                            if (found) {
                                ink_left = std::min(ink_left, glyph_left);
                                ink_right = std::max(ink_right, glyph_right);
                            } else {
                                ink_left = glyph_left;
                                ink_right = glyph_right;
                                found = true;
                            }
                        }
                    }

                    if (!found) {
                        ink_left = x + line.position.x();
                        ink_right = ink_left + w;
                    }

                    GLshort left = ink_left - NAMETAG_PAD;
                    GLshort right = ink_right + NAMETAG_PAD;
                    Color ntcolor = Color(0.0f, 0.0f, 0.0f, 0.6f);

                    // The plate rides with the ink it is behind, so it is submitted in the same
                    // device-pixel batch instead of being squeezed into the stretched picture.
                    emit_text(to_device(left),
                              to_device(right),
                              to_device(ink_top),
                              to_device(ink_bottom),
                              null_offset_,
                              ntcolor);
                    emit_text(to_device(left - 1),
                              to_device(left),
                              to_device(ink_top + 1),
                              to_device(ink_bottom - 1),
                              null_offset_,
                              ntcolor);
                    emit_text(to_device(right),
                              to_device(right + 1),
                              to_device(ink_top + 1),
                              to_device(ink_bottom - 1),
                              null_offset_,
                              ntcolor);
                }

                break;
            }
        }

        for (const Text::Layout::Line &line: layout) {
            Point<int16_t> position = line.position;

            for (const Text::Layout::Word &word: line.words) {
                GLshort ax = position.x() + layout.advance(word.first);
                GLshort ay = position.y();

                const GLfloat *wordcolor = nullptr;

                if (word.color < Color::Name::NUM_COLORS) {
                    wordcolor = Color::colors[word.color];
                } else {
                    wordcolor = Color::colors[colorid];
                }

                Color abscolor =
                        color * Color(wordcolor[0], wordcolor[1], wordcolor[2], 1.0f);

                for (size_t pos = word.first; pos < word.last; ++pos) {
                    auto c = text[pos];
                    const Glyph &ch = get_glyph(c, id);

                    if (ax == 0 && c == ' ') {
                        continue;
                    }

                    ax += ch.ax;

                    if (ch.nw <= 0 || ch.nh <= 0) {
                        continue;
                    }

                    // The pen is advanced in logical pixels, because that is what the layout counts
                    // in, and only the pen itself is snapped. Where the ink starts inside it and how
                    // far it reaches are taken from the bitmap's own device box, so the glyph is
                    // painted with the exact pixels the platform renderer produced.
                    const GLshort char_x = to_device(static_cast<int32_t>(x) + ax - ch.ax) + ch.dx;
                    const GLshort char_y = to_device(static_cast<int32_t>(y) + ay) - ch.dy;
                    GLshort char_right = static_cast<GLshort>(char_x + ch.nw);
                    GLshort char_bottom = static_cast<GLshort>(char_y + ch.nh);

                    Offset offset = ch.offset;

                    if (char_bottom > dev_max) {
                        const GLshort bottom_adjust = char_bottom - dev_max;

                        if (bottom_adjust < max_trim) {
                            offset.bottom = static_cast<GLshort>(offset.bottom - bottom_adjust);
                            char_bottom = static_cast<GLshort>(char_bottom - bottom_adjust);
                        } else {
                            continue;
                        }
                    }

                    if (char_y < dev_min) {
                        continue;
                    }

                    emit_text(char_x, char_right, char_y, char_bottom, offset, abscolor);
                }
            }
        }
    }

    void GraphicsGL::draw_rectangle(int16_t x,
                                    int16_t y,
                                    int16_t width,
                                    int16_t height,
                                    float red,
                                    float green,
                                    float blue,
                                    float alpha) {
        if (locked_) {
            return;
        }

        batch_quads().emplace_back(x,
                                   x + width,
                                   y,
                                   y + height,
                                   null_offset_,
                                   Color(red, green, blue, alpha),
                                   0.0f);
    }

    void GraphicsGL::draw_circle(int16_t x,
                                 int16_t y,
                                 int16_t radius,
                                 float red,
                                 float green,
                                 float blue,
                                 float alpha) {
        if (locked_) {
            return;
        }

        batch_circles().emplace_back(x, y, radius,
                                     Color(red, green, blue, alpha));
    }

    void GraphicsGL::draw_screen_fill(float red,
                                      float green,
                                      float blue,
                                      float alpha) {
        draw_rectangle(0, 0, VWIDTH, VHEIGHT, red, green, blue, alpha);
    }

    void GraphicsGL::lock() {
        locked_ = true;
    }

    void GraphicsGL::unlock() {
        locked_ = false;
    }

    void GraphicsGL::sync_screen_size() {
        if (locked_) {
            return;
        }

        int16_t new_width = Constants::Constants::get().get_viewwidth();
        int16_t new_height = Constants::Constants::get().get_viewheight();

        if (new_width == VWIDTH && new_height == VHEIGHT) {
            return;
        }

        VWIDTH = new_width;
        VHEIGHT = new_height;
        SCREEN = Rectangle<int16_t>(0, VWIDTH, 0, VHEIGHT);

        glUseProgram(shader_program_);
        glUniform2f(uniform_screen_size_, VWIDTH, VHEIGHT);
    }

    void GraphicsGL::begin_scene() {
        if (scene_width_ != VWIDTH || scene_height_ != VHEIGHT) {
            if (scene_fbo_) {
                glDeleteFramebuffers(1, &scene_fbo_);
                glDeleteTextures(1, &scene_tex_);
            }

            glGenTextures(1, &scene_tex_);
            glBindTexture(GL_TEXTURE_2D, scene_tex_);
            // The scene is magnified by a non-integer factor, so the blit interpolates between
            // source texels instead of duplicating them.
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, VWIDTH, VHEIGHT, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, nullptr);

            glGenFramebuffers(1, &scene_fbo_);
            glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo_);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   scene_tex_, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            scene_width_ = VWIDTH;
            scene_height_ = VHEIGHT;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo_);
        // The scene program maps its own coordinates onto whatever viewport is set, so the off-screen
        // pass has to be told the scene's size rather than the view box it will eventually fill.
        glViewport(0, 0, scene_width_, scene_height_);
        glScissor(0, 0, scene_width_, scene_height_);

        set_scene_attribs();
    }

    void GraphicsGL::set_scene_attribs() {
        bind_atlas(scene_atlas_);

        // There is no vertex array object backing either program, so the attribute pointers are
        // global state that the filter pass left pointing at its own quad.
        glBindBuffer(GL_ARRAY_BUFFER, VBO_);
        glVertexAttribPointer(attribute_coord_,
                              4,
                              GL_SHORT,
                              GL_FALSE,
                              sizeof(Quad::Vertex),
                              nullptr);
        glVertexAttribPointer(attribute_color_,
                              4,
                              GL_FLOAT,
                              GL_FALSE,
                              sizeof(Quad::Vertex),
                              (const void *) offsetof(Quad::Vertex, color));
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void GraphicsGL::end_scene(int32_t box_x, int32_t box_y, int32_t box_width,
                               int32_t box_height, int32_t surface_width,
                               int32_t surface_height, float opacity) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, surface_width, surface_height);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glViewport(box_x, box_y, box_width, box_height);
        glScissor(box_x, box_y, box_width, box_height);
        glEnable(GL_SCISSOR_TEST);

        // The scene is already blended into itself; blending it again over the black bars would
        // only wash out whatever alpha the last sprite left behind.
        glDisable(GL_BLEND);
        draw_filter(scene_tex_);
        glEnable(GL_BLEND);

        // World lettering goes onto the surface, not into the picture that was just stretched over
        // it: the scene has to be magnified to fit the box and text that rides along comes out of it
        // soft. Its coordinates are the box's own pixels, so it is one texel per pixel and as sharp
        // as the interface's.
        if (!text_quads_.empty()) {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            set_scene_attribs();
            flush_batch(text_quads_,
                        static_cast<int16_t>(box_width),
                        static_cast<int16_t>(box_height));
        }

        if (!UIRenderer::get().empty()) {
            // The interface goes onto the surface through the same single stretch the scene just
            // went through, never into the scene texture first - being resampled twice is what
            // made the buttons softer than the picture behind them. Unlike the scene blit this one
            // keeps blending, because it lies over a picture that is already there, and it blends
            // as premultiplied: its target was cleared to nothing, so what it holds is already
            // multiplied by its own alpha. Blending it as if it weren't would multiply twice and
            // leave a grey line along every hard edge the interface draws.
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            draw_filter(UIRenderer::get().texture());
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        if (!PanelRenderer::get().empty()) {
            // Over the interface: a window that opts into this pass is the thing being looked at, so
            // it covers whatever the interface happens to draw where it stands. What has to stay
            // above an enlarged window - notices, tooltips, status text - belongs to the game's own
            // interface rather than to the screen the enlarged window is standing on.
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            draw_filter(PanelRenderer::get().texture());
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        if (opacity != 1.0f) {
            const Color color = Color(0.0f, 0.0f, 0.0f, 1.0f - opacity);
            std::vector<Quad> fade;
            std::vector<Circle> fade_circles;

            // Applied last, so the transition darkens the interface along with the scene.
            fade.emplace_back(0,
                              static_cast<GLshort>(box_width),
                              0,
                              static_cast<GLshort>(box_height),
                              null_offset_,
                              color,
                              0.0f);
            fade_circles.emplace_back(1, 1, 1, color);

            set_scene_attribs();
            flush_batch(fade, fade_circles,
                        static_cast<int16_t>(box_width), static_cast<int16_t>(box_height));
        }

        // Bitmaps get packed into the atlas from Game::update(), which runs outside this frame, so
        // leaving a texture bound here would upload them into it instead.
        glBindTexture(GL_TEXTURE_2D, scene_atlas_.texture);
    }

    void GraphicsGL::draw_filter(GLuint source) {
        glUseProgram(filter_program_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, source);
        glUniform1i(filter_uniform_scene_, 0);

        glBindBuffer(GL_ARRAY_BUFFER, filter_vbo_);
        glEnableVertexAttribArray(filter_attribute_coord_);
        glVertexAttribPointer(filter_attribute_coord_, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisableVertexAttribArray(filter_attribute_coord_);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    GraphicsGL::Batch GraphicsGL::active_batch() {
        // The panel pass wins over the interface, and either over the scene, so one window can open
        // it from inside the interface's own bracket and hand the rest of the screen straight back.
        if (PanelRenderer::get().active()) {
            return Batch::PANEL;
        }

        return UIRenderer::get().active() ? Batch::UI : Batch::SCENE;
    }

    std::vector<GraphicsGL::Quad> &GraphicsGL::batch_quads() {
        switch (active_batch()) {
            case Batch::PANEL:
                return PanelRenderer::get().quads();
            case Batch::UI:
                return UIRenderer::get().quads();
            case Batch::SCENE:
            default:
                return quads_;
        }
    }

    std::vector<GraphicsGL::Circle> &GraphicsGL::batch_circles() {
        switch (active_batch()) {
            case Batch::PANEL:
                return PanelRenderer::get().circles();
            case Batch::UI:
                return UIRenderer::get().circles();
            case Batch::SCENE:
            default:
                return circles_;
        }
    }

    std::vector<GraphicsGL::Quad> &GraphicsGL::batch_text_quads() {
        // The two magnified passes keep their lettering in the same ordered batch as their geometry,
        // so the two still cover each other the way they were drawn; the scene has to hold its
        // lettering aside because that one is not composited until the stretched picture is already on
        // the surface.
        switch (active_batch()) {
            case Batch::PANEL:
                return PanelRenderer::get().quads();
            case Batch::UI:
                return UIRenderer::get().quads();
            case Batch::SCENE:
            default:
                return text_quads_;
        }
    }

    void GraphicsGL::flush_batch(const std::vector<Quad> &quads,
                                  int16_t screen_width,
                                  int16_t screen_height) {
        static const std::vector<Circle> no_circles;

        flush_batch(quads, no_circles, screen_width, screen_height);
    }

    void GraphicsGL::flush_batch(const std::vector<Quad> &quads,
                                  const std::vector<Circle> &circles,
                                  int16_t screen_width,
                                  int16_t screen_height) {
        submit(quads.data(), quads.size(), circles, screen_width, screen_height);
    }

    void GraphicsGL::flush_ui(const std::vector<Quad> &quads,
                              const std::vector<Circle> &circles,
                              int16_t layout_width,
                              int16_t layout_height,
                              int16_t target_width,
                              int16_t target_height) {
        size_t first = 0;
        bool circles_sent = circles.empty();

        while (first < quads.size()) {
            const bool text = quads[first].text;
            size_t last = first + 1;

            while (last < quads.size() && quads[last].text == text) {
                ++last;
            }

            const bool with_circles = !text && !circles_sent;
            circles_sent = circles_sent || with_circles;

            static const std::vector<Circle> no_circles;

            submit(quads.data() + first,
                   last - first,
                   with_circles ? circles : no_circles,
                   text ? target_width : layout_width,
                   text ? target_height : layout_height);

            first = last;
        }

        if (!circles_sent) {
            // A pass with nothing but lettering in it still has to draw its circles somewhere.
            submit(nullptr, 0, circles, layout_width, layout_height);
        }
    }

    void GraphicsGL::submit(const Quad *quads,
                            size_t count,
                            const std::vector<Circle> &circles,
                            int16_t screen_width,
                            int16_t screen_height) {
        if (count == 0 && circles.empty()) {
            return;
        }

        // A batch's coordinates are divided by this size to land in clip space, so drawing the
        // interface against a smaller screen is what magnifies every window, button and picture of
        // it together. Nothing else about the submission changes.
        glUseProgram(shader_program_);
        glUniform2f(uniform_screen_size_, screen_width, screen_height);

        std::vector<Quad::Vertex> triangles;
        std::vector<Circle::Vertex> circle_vertices;

        triangles.reserve(count * 6);
        for (size_t i = 0; i < count; ++i) {
            const Quad &quad = quads[i];

            triangles.push_back(quad.vertices[0]);
            triangles.push_back(quad.vertices[1]);
            triangles.push_back(quad.vertices[2]);

            triangles.push_back(quad.vertices[0]);
            triangles.push_back(quad.vertices[2]);
            triangles.push_back(quad.vertices[3]);
        }

        for (const Circle &circle: circles) {
            for (int i = 0; i < Circle::NUM_SEGMENTS; i++) {
                circle_vertices.push_back(circle.vertices[i]);
            }
        }

        glEnableVertexAttribArray(attribute_coord_);
        glEnableVertexAttribArray(attribute_color_);

        glBindBuffer(GL_ARRAY_BUFFER, VBO_);
        glBufferData(GL_ARRAY_BUFFER, triangles.size() * sizeof(Quad::Vertex),
                     triangles.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, triangles.size());

        glBindBuffer(GL_ARRAY_BUFFER, VBO_);
        glBufferData(GL_ARRAY_BUFFER, circle_vertices.size() * sizeof(Circle::Vertex),
                     circle_vertices.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLE_FAN, 0, circle_vertices.size());

        glDisableVertexAttribArray(attribute_coord_);
        glDisableVertexAttribArray(attribute_color_);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void GraphicsGL::flush() {
        // The bars around the view are cleared separately from it, so they stay black.
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        bind_atlas(scene_atlas_);
        flush_batch(quads_, circles_, VWIDTH, VHEIGHT);
        PanelRenderer::get().flush();
        UIRenderer::get().flush();
    }

    void GraphicsGL::clear_scene() {
        if (!locked_) {
            quads_.clear();
            circles_.clear();
            text_quads_.clear();
            PanelRenderer::get().clear();
            UIRenderer::get().clear();
        }
    }
}  // namespace ms