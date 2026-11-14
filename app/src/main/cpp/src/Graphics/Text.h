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

#include <map>
#include <vector>

#include "DrawArgument.h"

namespace ms {
class Text {
public:
    enum Font { A11M, A11B, A12M, A12B, A13M, A13B, A15B, A18M, NUM_FONTS };

    enum Alignment { LEFT, CENTER, RIGHT };

    enum Background { NONE, NAMETAG };

    // The pixel size and weight a font id stands for, in the units the pass lays itself out in. A
    // preview that is handed to the platform instead of being painted out of the glyph atlas carries
    // these with it, because the platform has to be told how big to letter the same words.
    static void metrics(Font id, float &px, bool &bold);

    class Layout {
    public:
        struct Word {
            size_t first;
            size_t last;
            Font font;
            Color::Name color;
        };

        struct Line {
            std::vector<Word> words;
            Point<int16_t> position;
        };

        Layout(std::vector<Line> lines,
               std::vector<int16_t> advances,
               int16_t width,
               int16_t height,
               int16_t endx,
               int16_t endy);

        Layout();

        // The same lines, pens and wrapping, lettering only the first `shown` code points. Laying the
        // part said so far out afresh would choose the breaks again from a shorter text, and a word
        // already on the screen would drop a line the moment the next one overflowed the column.
        Layout prefix(size_t shown) const;

        int16_t width() const;

        int16_t height() const;

        int16_t advance(size_t index) const;

        Point<int16_t> get_dimensions() const;

        Point<int16_t> get_endoffset() const;

        using iterator = std::vector<Line>::const_iterator;

        iterator begin() const;

        iterator end() const;

    private:
        std::vector<Line> lines_;
        std::vector<int16_t> advances_;
        Point<int16_t> dimensions_;
        Point<int16_t> end_offset_;
    };

    struct VerticalInk {
        int16_t top;
        int16_t bottom;
    };

    Text(Font font,
         Alignment alignment,
         Color::Name color,
         Background background,
         const std::string &text = "",
         uint16_t maxwidth = 0,
         bool formatted = true,
         int16_t line_adj = 0);

    Text(Font font,
         Alignment alignment,
         Color::Name color,
         const std::string &text = "",
         uint16_t maxwidth = 0,
         bool formatted = true,
         int16_t line_adj = 0);

    Text();

    void draw(const DrawArgument &args) const;

    void draw(const DrawArgument &args, const Range<int16_t> &vertical) const;

    void change_text(const std::string &text);

    void change_text(const std::vector<uint32_t> &t);

    // This text with only its first `shown` code points lettered, in the wrapping the whole of it was
    // laid out in: what a typewriter shows of a line without moving the words already on the screen.
    Text revealed(size_t shown) const;

    void change_color(Color::Name color);

    void set_background(Background background);

    bool empty() const;

    size_t length() const;

    int16_t width() const;

    // The ink box of the first laid-out line, relative to the draw position: what a NAMETAG plate
    // wraps. A neighbour that must keep a fixed gap from that plate measures from here, because a
    // glyph's side bearings are not part of the advance width() reports.
    Range<int16_t> ink_bounds() const;

    int16_t height() const;

    uint16_t advance(size_t pos) const;

    Point<int16_t> dimensions() const;

    Point<int16_t> end_offset() const;

    // Vertical extent of the actual glyph ink, relative to the y passed to draw().
    // Returns {0, 0} when the text has no ink at all.
    VerticalInk ink_vertical() const;

    const std::string &get_text() const;

    // The three things a native preview needs to reproduce this text itself: where each run starts,
    // which code points it holds, and how it was lettered. Wrapping and alignment are already
    // settled in the layout, so the platform is only ever asked to draw a run at a stated pen.
    const Layout &layout() const { return layout_; }

    const std::vector<uint32_t> &codepoints() const { return text_as_values_; }

    Font font() const { return font_; }

    Color::Name color() const { return color_; }

private:
    void reset_layout();

    Font font_;
    Alignment alignment_;
    Color::Name color_;
    Background background_;
    Layout layout_;
    uint16_t max_width_;
    bool formatted_;
    std::string text_;
    std::vector<uint32_t> text_as_values_;
    int16_t line_adj_;
};
}  // namespace ms