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

#include "GLES3/gl32.h"
#include <imgwz/bitmap.hpp>

#include "../Constants.h"
#include "../Error.h"
#include "../Util/QuadTree.h"
#include "Text.h"
#include "glfm.h"

namespace ms {
// Graphics engine which uses OpenGL
    class GraphicsGL : public Singleton<GraphicsGL> {
        // The interface pass lives in its own module but shares this GL device (shader, atlas, VBO)
        // and has to restore the scene framebuffer after it flushes, so it reaches the private
        // device pieces through friendship rather than through a widened public surface.
        friend class UIRenderer;

    public:
        GraphicsGL();

        // Initialize all resources
        Error init(GLFMDisplay *pApp);

        // Re-initialise after changing screen modes.
        void reinit();

        // Clear all bitmaps if most of the space is used up
        void clear();

        // Add a bitmap to the available resources.
        void add_bitmap(const img::bitmap &bmp);

        // Draw the bitmap with the given parameters.
        void draw(const img::bitmap &bmp,
                  const Rectangle<int16_t> &rect,
                  const Color &color,
                  float angle);

        // Create a layout for the text with the parameters specified.
        // Text::Layout create_layout(const std::string &text,
        //                            Text::Font font,
        //                            Text::Alignment alignment,
        //                            int16_t maxwidth,
        //                            bool formatted,
        //                            int16_t line_adj);

        Text::Layout create_layout(const std::vector<uint32_t> &text_as_values,
                                   Text::Font id,
                                   Text::Alignment alignment,
                                   int16_t maxwidth,
                                   bool formatted,
                                   int16_t line_adj);

        // Draw a text with the given parameters.
        // void draw_text(const DrawArgument &args,
        //                const Range<int16_t> &vertical,
        //                const std::string &text,
        //                const Text::Layout &layout,
        //                Text::Font font,
        //                Color::Name color,
        //                Text::Background back);

        void draw_text(const DrawArgument &args,
                       const Range<int16_t> &vertical,
                       const std::vector<uint32_t> &text,
                       const Text::Layout &layout,
                       Text::Font id,
                       Color::Name colorid,
                       Text::Background background);

        // Vertical extent of the real glyph ink in a laid-out string, relative to the y that would
        // be handed to draw_text. {0, 0} means the run carries no ink at all.
        Text::VerticalInk measure_ink_vertical(const std::vector<uint32_t> &text,
                                               const Text::Layout &layout,
                                               Text::Font id);

        // Draw a rectangle filled with the specified color.
        void draw_rectangle(int16_t x,
                            int16_t y,
                            int16_t width,
                            int16_t height,
                            float red,
                            float green,
                            float blue,
                            float alpha);

        // Draw a circle filled with the specified color.
        void draw_circle(int16_t x,
                         int16_t y,
                         int16_t radius,
                         float red,
                         float green,
                         float blue,
                         float alpha);

        // Fill the screen with the specified color.
        void draw_screen_fill(float red, float green, float blue, float alpha);

        // Lock the current scene
        void lock();

        // Unlock the scene.
        void unlock();

        // Submit the scene and interface batches into their own targets. The fade is applied by
        // end_scene(), once everything has been stretched onto the surface.
        void flush();

        // Adopt the resolution the game is set to. A transition that holds the scene frozen keeps
        // the coordinates it was drawn at, so the size only changes once the scene is live again.
        void sync_screen_size();

        // Adopt the size, in real surface pixels, of the letterboxed view box this frame is
        // composited onto. Each pass draws its text into a target of its own size, so the two text
        // densities come straight from this: the scene is stretched by box/view, the interface by
        // box/(view/UI_SCALE). A pass whose density moved throws its glyphs away, since the bitmaps
        // were rasterised for the old one.
        void set_output_size(int32_t box_width, int32_t box_height);

        // Start the pass the scene is drawn into. Filtering has to happen after the picture is
        // composited: every bitmap sits in one atlas with nothing between them, so smoothing a
        // sprite as it is drawn also pulls in its neighbour's edge and the tile seams come out as
        // lines across the screen.
        void begin_scene();

        // Stretch the scene and the interface over the letterboxed view box and leave the state as
        // the scene program expects to find it. Each of them is resampled exactly once, which is
        // what keeps the interface as sharp as the picture behind it.
        void end_scene(int32_t box_x, int32_t box_y, int32_t box_width, int32_t box_height,
                       int32_t surface_width, int32_t surface_height, float opacity);

        // The resolution the scene is drawn at, which is what the viewport has to fit.
        int16_t get_screen_width() const { return VWIDTH; }

        int16_t get_screen_height() const { return VHEIGHT; }

        // Clear the buffer contents.
        void clear_scene();

    private:
        struct Offset {
            GLshort left;
            GLshort right;
            GLshort top;
            GLshort bottom;

            Offset(GLshort x, GLshort y, GLshort width, GLshort height) :
                    left(x),
                    right(x + width),
                    top(y),
                    bottom(y + height) {}

            Offset() : left(0), right(0), top(0), bottom(0) {}
        };

        struct Leftover {
            GLshort left;
            GLshort right;
            GLshort top;
            GLshort bottom;

            Leftover(GLshort x, GLshort y, GLshort width, GLshort height) {
                left = x;
                right = x + width;
                top = y;
                bottom = y + height;
            }

            Leftover() {
                left = 0;
                right = 0;
                top = 0;
                bottom = 0;
            }

            GLshort width() const { return right - left; }

            GLshort height() const { return bottom - top; }
        };

        struct Quad {
            struct Vertex {
                // Local Space Position
                GLshort localcoord_x;
                GLshort localcoord_y;

                // Texture Coordinates
                GLshort texcoord_x;
                GLshort texcoord_y;

                Color color;
            };

            static const size_t LENGTH = 4;
            Vertex vertices[LENGTH];

            Quad(GLshort left,
                 GLshort right,
                 GLshort top,
                 GLshort bottom,
                 const Offset &offset,
                 const Color &color,
                 GLfloat rotation) {
                vertices[0] = {left, top, offset.left, offset.top, color};
                vertices[1] = {left, bottom, offset.left, offset.bottom, color};
                vertices[2] = {right, bottom, offset.right, offset.bottom, color};
                vertices[3] = {right, top, offset.right, offset.top, color};

                if (rotation != 0.0f) {
                    GLfloat cos = std::cos(rotation);
                    GLfloat sin = std::sin(rotation);
                    GLshort center_x = (left + right) / 2;
                    GLshort center_y = (top + bottom) / 2;

                    for (size_t i = 0; i < LENGTH; i++) {
                        GLshort vertice_x = vertices[i].localcoord_x - center_x;
                        GLshort vertice_y = vertices[i].localcoord_y - center_y;
                        GLfloat rounded_x =
                                std::roundf(vertice_x * cos - vertice_y * sin);
                        GLfloat rounded_y =
                                std::roundf(vertice_x * sin + vertice_y * cos);
                        vertices[i].localcoord_x =
                                static_cast<GLshort>(rounded_x + center_x);
                        vertices[i].localcoord_y =
                                static_cast<GLshort>(rounded_y + center_y);
                    }
                }
            }

            // Set for lettering, whose vertices are already in the target's own device pixels while
            // every other quad is laid out on the pass's smaller logical screen. The interface batch
            // keeps both in one vector so windows still cover each other in the order they drew, and
            // splits it into runs by this flag at submission.
            bool text = false;
        };

        struct Circle {
            struct Vertex {
                // Local Space Position
                GLshort localcoord_x;
                GLshort localcoord_y;

                // Texture Coordinates (not used for circles)
                GLshort texcoord_x;
                GLshort texcoord_y;

                Color color;
            };

            static const size_t NUM_SEGMENTS = 30; // Number of segments to approximate the circle
            Vertex vertices[NUM_SEGMENTS];

            Circle(GLshort center_x, GLshort center_y, GLshort radius, const Color &color) {
                // Calculate the angle between each vertex
                float angleIncrement = (2.0f * M_PI) / static_cast<float>(NUM_SEGMENTS);

                for (size_t i = 0; i < NUM_SEGMENTS; ++i) {
                    // Calculate the angle for this vertex
                    float angle = static_cast<float>(i) * angleIncrement;

                    // Calculate vertex position
                    GLshort vertex_x = center_x + static_cast<GLshort>(radius * std::cos(angle));
                    GLshort vertex_y = center_y + static_cast<GLshort>(radius * std::sin(angle));

                    // Set vertex data
                    vertices[i] = {vertex_x, vertex_y, 0, 0, color};
                }
            }
        };

        // One glyph's metrics plus the rect of its device-pixel bitmap in the atlas it was packed
        // into. The logical box (bw/bh/bl/bt) is what the layout and the nametag plate are measured
        // with; the device box (dx/dy/nw/nh) is what the glyph is actually drawn with, because a
        // glyph is rasterised at the exact density of the target it lands on and never stretched.
        struct Glyph {
            GLshort ax = 0;
            GLshort ay = 0;
            GLshort bw = 0;
            GLshort bh = 0;
            GLshort bl = 0;
            GLshort bt = 0;
            GLshort dx = 0;
            GLshort dy = 0;
            GLshort nw = 0;
            GLshort nh = 0;
            Offset offset;
        };

        // The platform-rendered bitmap a Glyph's metrics were derived from, kept so the strip of one
        // atlas never has to go back over JNI. It belongs to one atlas because its size is that
        // atlas's density; the logical fields are the same numbers divided back out of it.
        struct GlyphRaster {
            GLshort ax = 0;
            GLshort bl = 0;
            GLshort bt = 0;
            GLshort bw = 0;
            GLshort bh = 0;
            GLshort dx = 0;
            GLshort dy = 0;
            GLshort nw = 0;
            GLshort nh = 0;
            std::vector<uint8_t> alpha;
        };

        // Everything one texture of the device needs: its GL name, the quadtree/shelf packer for
        // sprites, and the strip plus cache for glyphs rasterised on demand. The scene and the
        // interface each own one so they can be filtered, grown and evicted independently.
        struct AtlasState {
            GLuint texture = 0;

            Point<GLshort> border;
            Range<GLshort> y_range;
            QuadTree<size_t, Leftover> leftovers;
            size_t rlid = 1;
            size_t wasted = 0;
            std::unordered_map<size_t, Offset> offsets;

            // Device pixels one logical pixel of this pass's coordinates covers. Glyphs are
            // rasterised at exactly this density and drawn in device pixels, so a glyph is one texel
            // per screen pixel and is never interpolated.
            float text_scale = 1.0f;
            std::unordered_map<uint64_t, GlyphRaster> rasters;

            // Bottom of the glyph strip; sprites are packed strictly below it so the shader's
            // fontregion test never mistakes one for the other.
            int16_t glyph_region_bottom = 0;
            int16_t glyph_x = 1;
            int16_t glyph_y = 1;
            int16_t glyph_row_h = 0;
            std::unordered_map<uint64_t, Glyph> glyphs;
        };

        void clear_internal(AtlasState &atlas);

        // Create (or leave) the texture and rewind both packers of one atlas.
        void init_atlas(AtlasState &atlas, bool linear);

        // Bind one atlas and tell the shader its size and where its glyph strip ends.
        void bind_atlas(AtlasState &atlas);

        void reset_glyph_region(AtlasState &atlas);

        // Give one atlas the device pixels per logical pixel it is about to be stretched by, and
        // rewind its glyph strip if that is not the density its glyphs were made for.
        void set_atlas_text_scale(AtlasState &atlas, float scale);

        // The atlas the current draw call belongs to: the interface while its pass is open, the
        // scene otherwise.
        AtlasState &active_atlas();

        const Offset &get_offset(AtlasState &atlas, const img::bitmap &bmp);

        // Metrics and the platform bitmap behind them, rasterised at the density of one atlas and
        // cached in it. Rasterises over JNI on first use.
        const GlyphRaster &get_raster(uint32_t codepoint, Text::Font id, AtlasState &atlas);

        // The advance one code point costs in a face, in logical pixels. Measured at the logical
        // size and shared by both atlases, because this is the one text metric that must not depend
        // on density: a layout counts in it, while the glyph is drawn from whichever atlas it ends
        // up in.
        int16_t glyph_advance(uint32_t codepoint, Text::Font id);

        // get_raster plus the glyph packed into the active atlas, ready to draw.
        const Glyph &get_glyph(uint32_t codepoint, Text::Font id);

        // Logical line height for a face.
        int16_t font_linespace(Text::Font id) const;

        // The logical pixel size and weight a Text::Font id stands for.
        static void font_metrics(Text::Font id, float &px, bool &bold);

        class LayoutBuilder {
        public:
            LayoutBuilder(GraphicsGL &graphics,
                          Text::Font id,
                          Text::Alignment alignment,
                          int16_t maxwidth,
                          bool formatted,
                          int16_t line_adj);

            // size_t add(const char *text, size_t prev, size_t first, size_t last);

            size_t add(const unsigned int *text,
                       size_t prev,
                       size_t first,
                       size_t last);

            Text::Layout finish(size_t first, size_t last);

        private:
            void add_word(size_t first,
                          size_t last,
                          Text::Font font,
                          Color::Name color);

            void add_line();

            GraphicsGL &graphics_;
            Text::Font id_;

            Text::Alignment alignment_;
            Text::Font font_id_;
            Color::Name color_;
            int16_t max_width_;
            bool formatted_;

            int16_t ax_;
            int16_t ay_;

            std::vector<Text::Layout::Line> lines_;
            std::vector<Text::Layout::Word> words_;
            std::vector<int16_t> advances_;
            int16_t width_;
            int16_t endy_;
            int16_t line_adj_;
        };

        int16_t VWIDTH;
        int16_t VHEIGHT;
        Rectangle<int16_t> SCREEN;

        static const GLshort ATLASW = 8192;
        static const GLshort ATLASH = 8192;
        static const GLshort MINLOSIZE = 32;

        // Height of the strip at the top of each atlas reserved for glyphs the platform renderer
        // rasterises on demand. Everything the shader sees at or above this row is treated as text;
        // sprites are packed below it.
        static const GLshort GLYPH_REGION_H = 512;

        bool locked_;

        std::vector<Quad>& batch_quads();

        std::vector<Circle>& batch_circles();

        std::vector<Quad>& batch_text_quads();

        // Submit one batch, mapping its coordinates onto a screen of the given size.
        void flush_batch(const std::vector<Quad> &quads,
                         const std::vector<Circle> &circles,
                         int16_t screen_width,
                         int16_t screen_height);

        // The same with no circles, for a text batch.
        void flush_batch(const std::vector<Quad> &quads,
                         int16_t screen_width,
                         int16_t screen_height);

        // Submit the interface batch. Geometry is mapped from the UI_SCALE-smaller layout screen and
        // lettering from the target's own pixels, so the batch is walked in submission order and
        // broken into runs of the two; keeping the order is what lets a window drawn later cover a
        // window drawn earlier. Circles are logical-space and ride with the first geometry run.
        void flush_ui(const std::vector<Quad> &quads,
                      const std::vector<Circle> &circles,
                      int16_t layout_width,
                      int16_t layout_height,
                      int16_t target_width,
                      int16_t target_height);

        // One glDrawArrays of quads, with the vertices held in a vector rather than a batch, so a run
        // of a mixed batch can be submitted on its own.
        void submit(const Quad *quads,
                    size_t count,
                    const std::vector<Circle> &circles,
                    int16_t screen_width,
                    int16_t screen_height);

        // Leave the scene program, its atlas and its attribute pointers bound, as the next batch
        // submission and the bitmaps packed outside the frame both expect.
        void set_scene_attribs();

        // Stretch a texture over whatever viewport is currently set, through the filter program.
        void draw_filter(GLuint source);

        std::vector<Quad> quads_;
        std::vector<Circle> circles_;

        // Text is batched apart from everything else. Its vertices are in the target's own device
        // pixels while every other quad is in the pass's logical pixels, so the two need a different
        // screensize uniform and the scene's share has to wait for the surface rather than go into
        // the scene texture that gets stretched.
        std::vector<Quad> text_quads_;

        GLuint VBO_;

        GLint shader_program_;
        GLint attribute_coord_;
        GLint attribute_color_;
        GLint uniform_texture_;
        GLint uniform_atlas_size_;
        GLint uniform_screen_size_;
        GLint uniform_yoffset;
        GLint uniform_font_region_;

        Offset null_offset_;

        // The scene is composited into this texture and only then stretched to the screen, so the
        // filter never sees two neighbouring bitmaps bleeding into each other.
        GLuint scene_fbo_;
        GLuint scene_tex_;
        int16_t scene_width_;
        int16_t scene_height_;

        GLuint filter_vbo_;
        GLint filter_program_;
        GLint filter_attribute_coord_;
        GLint filter_uniform_scene_;

        // The scene and the interface each pack their sprites and glyphs into their own texture, so
        // the two passes never share a filter setting or evict each other's bitmaps.
        AtlasState scene_atlas_;
        AtlasState ui_atlas_;

        // Scratch for the one-texel replicated border, kept at the size of the largest sprite so
        // that packing a picture never has to allocate and zero a fresh buffer for it.
        std::vector<uint8_t> atlas_scratch_;

        // Scratch for a glyph upload: its bitmap inside a frame of transparent texels, so the gutter
        // the packer leaves around it is defined instead of whatever the driver left there.
        std::vector<uint8_t> glyph_scratch_;

        // Logical advances, keyed by face and code point. Survives a density change: it is the one
        // metric a layout already built has to keep agreeing with.
        std::unordered_map<uint64_t, int16_t> advance_cache_;
    };
}  // namespace ms