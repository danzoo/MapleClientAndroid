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
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

#include <android/log.h>

#include "../Configuration.h"
#include "AndroidText.h"
#include "UIRenderer.h"
#include "Window.h"

namespace ms {
    GraphicsGL::GraphicsGL() : locked_(false) {
        VWIDTH = Constants::Constants::get().get_viewwidth();
        VHEIGHT = Constants::Constants::get().get_viewheight();
        SCREEN = Rectangle<int16_t>(0, VWIDTH, 0, VHEIGHT);

        scene_fbo_ = 0;
        scene_tex_ = 0;
        scene_width_ = 0;
        scene_height_ = 0;

        text_scale_ = Constants::UI_SCALE;
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
        // interpolates instead of showing stair-steps.
        init_atlas(scene_atlas_, false);
        init_atlas(ui_atlas_, true);

        if (!AndroidText::init(pApp)) {
            __android_log_print(ANDROID_LOG_ERROR, "NativeText", "bridge init failed");
        }

        return Error::Code::NONE;
    }

    void GraphicsGL::init_atlas(AtlasState &atlas, bool linear) {
        if (atlas.texture == 0) {
            glGenTextures(1, &atlas.texture);
        }

        glBindTexture(GL_TEXTURE_2D, atlas.texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA,
                     ATLASW,
                     ATLASH,
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
        // Sprites are packed from the first row under the glyph strip. It has to be strictly below
        // the fontregion the shader compares against, or the top sprite row would be sampled as a
        // glyph (white times its red channel) instead of as a picture.
        atlas.border = Point<GLshort>(0, static_cast<GLshort>(atlas.glyph_region_bottom + 1));
        atlas.y_range = Range<GLshort>();
        atlas.offsets.clear();
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
        glUniform2f(uniform_atlas_size_, ATLASW, ATLASH);
        glUniform1i(uniform_font_region_, atlas.glyph_region_bottom);
    }

    GraphicsGL::AtlasState &GraphicsGL::active_atlas() {
        return UIRenderer::get().active() ? ui_atlas_ : scene_atlas_;
    }

    void GraphicsGL::set_text_scale(float scale) {
        if (scale <= 0.0f) {
            return;
        }

        if (std::abs(scale - text_scale_) > 0.01f) {
            text_scale_ = scale;

            // Logical metrics are density independent, so layouts already built stay valid; only the
            // bitmaps were rasterised at the old density and have to be redone in both atlases.
            rasters_.clear();

            for (AtlasState *atlas : {&scene_atlas_, &ui_atlas_}) {
                atlas->glyphs.clear();
                reset_glyph_region(*atlas);
            }
        }
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

    const GraphicsGL::GlyphRaster &GraphicsGL::get_raster(uint32_t codepoint, Text::Font id) {
        uint64_t key = (static_cast<uint64_t>(id) << 32) | codepoint;

        auto cached = rasters_.find(key);

        if (cached != rasters_.end()) {
            return cached->second;
        }

        float px;
        bool bold;
        font_metrics(id, px, bold);

        GlyphBitmap bmp;
        AndroidText::rasterize(codepoint, px * text_scale_, bold, bmp);

        GlyphRaster raster;

        // Advance is always meaningful, even for blank glyphs (a space has no ink but still moves
        // the pen). Convert back to logical pixels so layout stays density independent.
        raster.ax = static_cast<GLshort>(std::lround(bmp.advance / text_scale_));

        if (bmp.width > 0 && bmp.height > 0) {
            raster.bl = static_cast<GLshort>(std::lround(bmp.bearing_x / text_scale_));
            raster.bt = static_cast<GLshort>(std::lround(bmp.bearing_y / text_scale_));
            raster.bw = static_cast<GLshort>(std::lround(static_cast<float>(bmp.width) / text_scale_));
            raster.bh = static_cast<GLshort>(std::lround(static_cast<float>(bmp.height) / text_scale_));
            raster.nw = bmp.width;
            raster.nh = bmp.height;
            raster.alpha = std::move(bmp.alpha);
        }

        return rasters_.emplace(key, std::move(raster)).first->second;
    }

    const GraphicsGL::Glyph &GraphicsGL::get_glyph(uint32_t codepoint, Text::Font id) {
        const GlyphRaster &raster = get_raster(codepoint, id);
        uint64_t key = (static_cast<uint64_t>(id) << 32) | codepoint;
        AtlasState &atlas = active_atlas();

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

        if (raster.nw > 0 && raster.nh > 0) {
            GLshort w = static_cast<GLshort>(raster.nw);
            GLshort h = static_cast<GLshort>(raster.nh);

            // Shelf-pack into this atlas's glyph strip. A glyph too wide for the rest of the row
            // starts a new row; running off the bottom throws the strip away and starts over, which
            // is cheap because the raster is already cached and only the upload is redone.
            if (atlas.glyph_x + w + 1 > ATLASW) {
                atlas.glyph_y = static_cast<int16_t>(atlas.glyph_y + atlas.glyph_row_h + 1);
                atlas.glyph_x = 1;
                atlas.glyph_row_h = 0;
            }

            if (atlas.glyph_y + h + 1 > atlas.glyph_region_bottom) {
                atlas.glyphs.clear();
                reset_glyph_region(atlas);
            }

            std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);

            for (size_t i = 0; i < static_cast<size_t>(w) * h; i++) {
                uint8_t coverage = raster.alpha[i];
                rgba[i * 4 + 0] = coverage;
                rgba[i * 4 + 1] = coverage;
                rgba[i * 4 + 2] = coverage;
                rgba[i * 4 + 3] = 255;
            }

            glBindTexture(GL_TEXTURE_2D, atlas.texture);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(GL_TEXTURE_2D,
                            0,
                            atlas.glyph_x,
                            atlas.glyph_y,
                            w,
                            h,
                            GL_RGBA,
                            GL_UNSIGNED_BYTE,
                            rgba.data());

            glyph.offset = Offset(atlas.glyph_x, atlas.glyph_y, w, h);

            atlas.glyph_x = static_cast<int16_t>(atlas.glyph_x + w + 1);

            if (h > atlas.glyph_row_h) {
                atlas.glyph_row_h = h;
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

        bind_atlas(scene_atlas_);
    }

    void GraphicsGL::clear() {
        for (AtlasState *atlas : {&scene_atlas_, &ui_atlas_}) {
            size_t used = ATLASW * atlas->border.y() + atlas->border.x() * atlas->y_range.second();
            double usedpercent = static_cast<double>(used) / (ATLASW * ATLASH);

            // usedpercent is a 0..1 fraction, so the threshold has to be one too. Comparing it
            // against 80.0 kept this reclaim dead, which left the mid-frame overflow inside
            // get_offset as the only way to free the atlas, and that one invalidates the offsets
            // of quads already batched for the frame being drawn.
            if (usedpercent > 0.80) {
                clear_internal(*atlas);
            }
        }
    }

    void GraphicsGL::add_bitmap(const nl::bitmap &bmp) {
        get_offset(scene_atlas_, bmp);
    }

    const GraphicsGL::Offset &GraphicsGL::get_offset(AtlasState &atlas, const nl::bitmap &bmp) {
        size_t id = bmp.id();
        auto offiter = atlas.offsets.find(id);

        if (offiter != atlas.offsets.end()) {
            return offiter->second;
        }

        GLshort x = 0;
        GLshort y = 0;
        GLshort width = bmp.width();
        GLshort height = bmp.height();

        if (width <= 0 || height <= 0) {
            return null_offset_;
        }

        // Decode before reserving any atlas area. A canvas with no payload (a blank animation
        // frame) has nothing to upload, and packing it first burned atlas space on every attempt
        // while caching nothing, so the atlas filled up and was wiped over and over.
        const uint8_t *src = static_cast<const uint8_t *>(bmp.data());

        if (!src) {
            // A canvas whose payload cannot be decoded stays unpainted instead of taking the
            // whole frame down with it. Caching the empty offset also stops the decode from
            // being retried on every frame; it is dropped with the rest of the cache on clear.
            return atlas.offsets.emplace(id, null_offset_).first->second;
        }

        // Bitmaps are stored with a one-texel border of replicated edge pixels. Texture
        // coordinates address texel corners, so a filtered sample taken exactly on a sprite's
        // edge would otherwise blend with whichever sprite happens to be packed next to it in
        // the atlas; the replicated border makes those edge samples resolve to the sprite's
        // own edge colour. This is what keeps a stretched one-pixel fill (tooltip bodies) and
        // abutting nine-slice pieces (window borders) free of foreign-colour fringes.
        const GLshort pw = static_cast<GLshort>(width + 2);
        const GLshort ph = static_cast<GLshort>(height + 2);

        Leftover value = Leftover(x, y, pw, ph);

        size_t lid = atlas.leftovers.findnode(
                value,
                [](const Leftover &val, const Leftover &leaf) {
                    return val.width() <= leaf.width() && val.height() <= leaf.height();
                });

        if (lid > 0) {
            const Leftover &leftover = atlas.leftovers[lid];

            x = leftover.left;
            y = leftover.top;

            GLshort width_delta = leftover.width() - pw;
            GLshort height_delta = leftover.height() - ph;

            atlas.leftovers.erase(lid);

            atlas.wasted -= pw * ph;

            if (width_delta >= MINLOSIZE && height_delta >= MINLOSIZE) {
                atlas.leftovers.add(
                        atlas.rlid,
                        Leftover(x + pw, y + ph, width_delta, height_delta));
                atlas.rlid++;

                if (pw >= MINLOSIZE) {
                    atlas.leftovers.add(atlas.rlid,
                                        Leftover(x, y + ph, pw, height_delta));
                    atlas.rlid++;
                }

                if (ph >= MINLOSIZE) {
                    atlas.leftovers.add(atlas.rlid,
                                        Leftover(x + pw, y, width_delta, ph));
                    atlas.rlid++;
                }
            } else if (width_delta >= MINLOSIZE) {
                atlas.leftovers.add(
                        atlas.rlid,
                        Leftover(x + pw, y, width_delta, ph + height_delta));
                atlas.rlid++;
            } else if (height_delta >= MINLOSIZE) {
                atlas.leftovers.add(
                        atlas.rlid,
                        Leftover(x, y + ph, pw + width_delta, height_delta));
                atlas.rlid++;
            }
        } else {
            if (atlas.border.x() + pw > ATLASW) {
                atlas.border.set_x(0);
                atlas.border.shift_y(atlas.y_range.second());

                if (atlas.border.y() + ph > ATLASH) {
                    clear_internal(atlas);
                } else {
                    atlas.y_range = Range<GLshort>();
                }
            }

            x = atlas.border.x();
            y = atlas.border.y();

            atlas.border.shift_x(pw);

            if (ph > atlas.y_range.second()) {
                if (x >= MINLOSIZE && ph - atlas.y_range.second() >= MINLOSIZE) {
                    atlas.leftovers.add(atlas.rlid,
                                        Leftover(0,
                                                 atlas.y_range.first(),
                                                 x,
                                                 ph - atlas.y_range.second()));
                    atlas.rlid++;
                }

                atlas.wasted += x * (ph - atlas.y_range.second());

                atlas.y_range = Range<int16_t>(y + ph, ph);
            } else if (ph < atlas.y_range.first() - y) {
                if (pw >= MINLOSIZE
                    && atlas.y_range.first() - y - ph >= MINLOSIZE) {
                    atlas.leftovers.add(atlas.rlid,
                                        Leftover(x,
                                                 y + ph,
                                                 pw,
                                                 atlas.y_range.first() - y - ph));
                    atlas.rlid++;
                }

                atlas.wasted += pw * (atlas.y_range.first() - y - ph);
            }
        }

        // The border is built in one reusable scratch buffer. A per-bitmap vector would have to be
        // mmapped, zeroed and munmapped again for every sprite, and the login animation frames are
        // large enough for that to dominate the time spent painting the logo.
        atlas_scratch_.resize(static_cast<size_t>(pw) * ph * 4);
        uint8_t *dst = atlas_scratch_.data();

        const size_t src_stride = static_cast<size_t>(width) * 4;
        const size_t dst_stride = static_cast<size_t>(pw) * 4;

        for (GLshort row = 0; row < ph; row++) {
            // Row 0 repeats the first source row and row ph-1 the last; the outer columns repeat
            // the first and last pixel of their row.
            const GLshort srow = std::min<GLshort>(std::max<GLshort>(row - 1, 0), height - 1);
            const uint8_t *srowp = src + static_cast<size_t>(srow) * src_stride;
            uint8_t *drowp = dst + static_cast<size_t>(row) * dst_stride;

            std::memcpy(drowp + 4, srowp, src_stride);
            std::memcpy(drowp, srowp, 4);
            std::memcpy(drowp + 4 + src_stride, srowp + src_stride - 4, 4);
        }

        glBindTexture(GL_TEXTURE_2D, atlas.texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D,
                        0,
                        x,
                        y,
                        pw,
                        ph,
                        GL_RGBA,
                        GL_UNSIGNED_BYTE,
                        dst);

        return atlas.offsets
                .emplace(std::piecewise_construct,
                         std::forward_as_tuple(id),
                         std::forward_as_tuple(static_cast<GLshort>(x + 1),
                                               static_cast<GLshort>(y + 1),
                                               width,
                                               height))
                .first->second;
    }

    void GraphicsGL::draw(const nl::bitmap &bmp,
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

        batch_quads().emplace_back(rect.left(),
                                   rect.right(),
                                   rect.top(),
                                   rect.bottom(),
                                   get_offset(active_atlas(), bmp),
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

                wordwidth += graphics_.get_raster(c, id_).ax;

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
            const GlyphRaster &ch = graphics_.get_raster(c, id_);

            advances_.push_back(ax_);

            if (pos < first + skip || newline && c == ' ') {
                continue;
            }

            if (c < 32) {
                continue;
            }

            ax_ += ch.ax;

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

                    batch_quads().emplace_back(left,
                                               right,
                                               ink_top,
                                               ink_bottom,
                                               null_offset_,
                                               ntcolor,
                                               0.0f);
                    batch_quads().emplace_back(left - 1,
                                               left,
                                               ink_top + 1,
                                               ink_bottom - 1,
                                               null_offset_,
                                               ntcolor,
                                               0.0f);
                    batch_quads().emplace_back(right,
                                               right + 1,
                                               ink_top + 1,
                                               ink_bottom - 1,
                                               null_offset_,
                                               ntcolor,
                                               0.0f);
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

                    GLshort char_x = x + ax + ch.bl;
                    GLshort char_y = y + ay - ch.bt;
                    GLshort char_width = ch.bw;
                    GLshort char_height = ch.bh;
                    GLshort char_bottom = char_y + char_height;

                    Offset offset = ch.offset;

                    if (char_bottom > maxheight) {
                        GLshort bottom_adjust = char_bottom - maxheight;

                        if (bottom_adjust < 10) {
                            offset.bottom -= bottom_adjust;
                            char_bottom -= bottom_adjust;
                        } else {
                            continue;
                        }
                    }

                    if (char_y < minheight) {
                        continue;
                    }

                    if (ax == 0 && c == ' ') {
                        continue;
                    }

                    ax += ch.ax;

                    if (char_width <= 0 || char_height <= 0) {
                        continue;
                    }

                    batch_quads().emplace_back(char_x,
                                               char_x + char_width,
                                               char_y,
                                               char_bottom,
                                               offset,
                                               abscolor,
                                               0.0f);
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

    std::vector<GraphicsGL::Quad> &GraphicsGL::batch_quads() {
        return UIRenderer::get().active() ? UIRenderer::get().quads() : quads_;
    }

    std::vector<GraphicsGL::Circle> &GraphicsGL::batch_circles() {
        return UIRenderer::get().active() ? UIRenderer::get().circles() : circles_;
    }

    void GraphicsGL::flush_batch(const std::vector<Quad> &quads,
                                  const std::vector<Circle> &circles,
                                  int16_t screen_width,
                                  int16_t screen_height) {
        if (quads.empty() && circles.empty()) {
            return;
        }

        // A batch's coordinates are divided by this size to land in clip space, so drawing the
        // interface against a smaller screen is what magnifies every window, button and letter of
        // it together. Nothing else about the submission changes.
        glUseProgram(shader_program_);
        glUniform2f(uniform_screen_size_, screen_width, screen_height);

        std::vector<Quad::Vertex> triangles;
        std::vector<Circle::Vertex> circle_vertices;
        for (const Quad &quad: quads) {
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
        UIRenderer::get().flush();
    }
    void GraphicsGL::clear_scene() {
        if (!locked_) {
            quads_.clear();
            circles_.clear();
            UIRenderer::get().clear();
        }
    }
}  // namespace ms