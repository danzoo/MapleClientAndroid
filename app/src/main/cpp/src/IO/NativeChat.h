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

#include "../Graphics/Color.h"
#include "Messages.h"

namespace ms {
    // Everything the player reads about what happened, and the one place a line is typed into:
    // android.widget views on top of the game surface rather than a strip drawn out of the glyph
    // atlas. The atlas magnifies eleven-unit lettering on the way to the frame, which is what makes
    // a wall of text read soft; a platform view is laid out at the size it is finally drawn at, and
    // gets real line breaking, a scrolling list, the user's own font and the system's own keyboard
    // along with it.
    //
    // The render thread posts a line and never waits on the UI thread for anything. What is typed
    // comes back the other way through a queue that this class drains on its own update, so a line
    // is put on the wire by the thread that owns the connection.
    class NativeChat {
    public:
        // Which page of the window a line belongs to. GENERAL has no page of its own, matching the
        // chat bar it replaces: plain speech was only ever visible under ALL.
        enum Category : int32_t {
            GENERAL,
            BATTLE,
            PARTY,
            FRIEND,
            GUILD,
            ALLIANCE,
            NOTICE,
            NUM_CATEGORIES
        };

        // The colour a line is drawn in. Numbered as the chat bar numbered them, because a line
        // arriving off the wire carries this number in it.
        enum LineType { UNK0, WHITE, RED, BLUE, YELLOW, GREEN };

        // The window goes and comes with the game scene: a map asks for it, leaving for the login
        // page takes it back.
        static void activate();

        static void deactivate();

        // Once per update. Brings the window in when the scene behind it has finished fading in,
        // walks the message cooldowns down, and sends off whatever was typed into the window.
        static void update();

        static bool available();

        static void send_line(const std::string &line,
                              LineType type,
                              Category category);

        // One of the game's own notices - a skill that fell through, a scroll that failed. The same
        // notice repeated inside the cooldown is dropped rather than posted again.
        static void display_message(Messages::Type line, LineType type);

        // Out of the reader's way. The window cannot be put under the interface - android composites
        // it above the surface the game draws into whatever the game draws there - so going under it
        // is a matter of fading and of letting touches reach the game underneath.
        static void sink();

        // Back on top, because the reader asked for it.
        static void raise();

        // Whether a press at this position, in the units the interface works in, lands on the window
        // as it is currently placed and worn. The game only ever gets to ask about a window that is
        // out of the way; while it is on top it answers for its own touches.
        static bool covers(int16_t x, int16_t y);

    private:
        static bool prepare();

        static bool open();

        static void close();

        static void post(Category category, Color::Name color, const std::string &line);
    };
}  // namespace ms
