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

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "../Graphics/Color.h"
#include "../Graphics/DrawArgument.h"
#include "../Template/Point.h"

namespace ms {
    class Text;
    class Texture;

    // The item, equipment and skill previews, put on the screen by android's own views rather than
    // painted out of the glyph atlas and the interface's stretched picture. What the preview says and
    // where every piece of it goes is still worked out here, in the units the interface lays itself
    // out in; the platform is only ever asked to fill a rectangle, letter a run of text at a stated
    // pen, or draw one of the game's own bitmaps into a stated box. Nothing about the look is
    // re-decided on the other side of the bridge, which is what keeps it the same window it was.
    class NativeTooltip {
    public:
        // One preview's worth of work, in the order it would have been painted.
        class List {
        public:
            // A piece of the shared art. The name is the picture's path inside the archive, which is
            // also the file it was exported to - so the two sides cannot drift apart by accident.
            void picture(const Texture &source,
                         const DrawArgument &where,
                         const std::string &asset);

            // A picture that only this preview has: an item's or a skill's icon, which lives in an
            // archive the platform cannot open and so travels over as pixels.
            void icon(const Texture &source, const DrawArgument &where);

            void block(Point<int16_t> top_left,
                       Point<int16_t> size,
                       Color::Name color,
                       float opacity);

            // One text block: every run the layout produced, at the pen the layout counted out.
            void text(const Text &content, Point<int16_t> origin);

            bool empty() const;

        private:
            friend class NativeTooltip;

            enum Op : int32_t { TEXT, BLOCK, PICTURE, ICON };

            void push(int32_t kind,
                      int32_t a,
                      int32_t b,
                      int32_t c,
                      int32_t x1,
                      int32_t y1,
                      int32_t x2,
                      int32_t y2);

            std::vector<int32_t> ops_;
            std::vector<std::string> texts_;
            std::vector<std::string> names_;
            std::vector<int32_t> pixels_;
            // Which icons have already been decoded out of the archive for this preview, and where
            // their pixels ended up.
            std::unordered_map<size_t, int32_t> icons_;
        };

        // The window comes and goes with the game scene, exactly like the message window does. It is
        // one window the size of the whole surface that never moves and never resizes: an android
        // window that changes its frame shows the previous picture squeezed into the new one for a
        // frame or two, and a preview that grows and shrinks with its content would do that on every
        // tap.
        static void activate();

        static void deactivate();

        static bool available();

        // Puts a preview on the screen. An empty list takes it off. The caller is the one that knows
        // when either is a change, so this is asked for once per change rather than once per frame.
        static void show(const List &list);

    private:
        static bool prepare();

        static bool open();

        static void close();
    };
}  // namespace ms
