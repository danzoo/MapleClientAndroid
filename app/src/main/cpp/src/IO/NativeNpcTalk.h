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

#include "../../Graphics/Color.h"
#include "../../Graphics/DrawArgument.h"
#include "../../Template/Point.h"
#include "../../Template/Range.h"

namespace ms {
    class Text;
    class Texture;

    // The dialog a non-player character talks through, put on the screen by android's own views
    // rather than painted out of the glyph atlas.
    //
    // Same arrangement as the item previews: what the dialog says and where every piece of it goes is
    // worked out here, in the units the interface lays itself out in, and the platform is only ever
    // asked to draw one of the game's own bitmaps into a stated box, or letter a run of text at a
    // stated pen. Nothing about the look is re-decided on the other side of the bridge.
    //
    // Reading is the one thing that does not move. The buttons, the scroll bar and the answer options
    // keep being hit-tested by the game, because the window this is drawn into is above the game's
    // surface - art the game still paints would be hidden behind it, so anything that used to be
    // painted on top of the dialog's own frame has to travel over too.
    class NativeNpcTalk {
    public:
        // One dialog's worth of work, in the order it would have been painted.
        class List {
        public:
            // A piece of the shared chrome. The name is the picture's path inside the archive, which
            // is also the file it was exported to.
            void picture(const Texture &source,
                         const DrawArgument &where,
                         const std::string &asset);

            // A picture only this dialog has - the character being spoken by, which lives in an
            // archive the platform cannot open and so travels over as pixels. Being mirrored is read
            // out of the draw argument, the same one the pass would have flipped with.
            void icon(const Texture &source, const DrawArgument &where);

            // One block of lettering. A clip band keeps the run inside the frame the way the pass
            // clipped it, which is what makes a dialog longer than the box readable: the words that
            // have scrolled past simply are not shown.
            void text(const Text &content,
                      Point<int16_t> origin,
                      Range<int16_t> clip = Range<int16_t>());

            bool empty() const;

        private:
            friend class NativeNpcTalk;

            enum Op : int32_t { TEXT, TEXT_CLIPPED, PICTURE, ICON, ICON_FLIPPED };

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
            std::unordered_map<size_t, int32_t> icons_;
        };

        // Puts a dialog on the screen, building the window the first time one is asked for. An empty
        // list takes the words off but leaves the window standing.
        static void show(const List &list);

        // Takes the window down. Called when the dialog goes away rather than when it changes, so
        // that paging through what a character is saying does not cost a new window each time.
        static void dismiss();

        static bool available();

    private:
        static bool prepare();

        static bool open();

        static void close();
    };
}  // namespace ms
