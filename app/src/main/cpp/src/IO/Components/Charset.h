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

#include <unordered_map>

#include "../Graphics/Texture.h"
#include "../NativeTooltip.h"

namespace ms {
class Charset {
public:
    enum Alignment { LEFT, CENTER, RIGHT };

    // A run of the game's own bitmapped lettering. The asset is the path the set lives at inside its
    // archive, which is also the folder its canvases were exported to - a character table can only
    // be handed to a native window one name at a time, and the name a character was found under is
    // not the name it was saved as (the requirement sets call a percent sign "%" and the summary
    // sets call it "percent", and both are looked up by their first letter).
    Charset(const img::node &source, Alignment alignment, const std::string &asset = std::string());

    Charset();

    void draw(int8_t character, const DrawArgument &args) const;

    int16_t draw(const std::string &text, const DrawArgument &args) const;

    int16_t draw(const std::string &text,
                 int16_t hspace,
                 const DrawArgument &args) const;

    // The same run, counted out the same way, written into a preview's display list. The value
    // returned is the pen the run left behind, exactly as the pass above leaves it, because callers
    // place the next piece against it.
    int16_t build(NativeTooltip::List &list,
                  const std::string &text,
                  int16_t hspace,
                  const DrawArgument &args) const;

    int16_t getw(int8_t character) const;

private:
    void build_character(NativeTooltip::List &list,
                         int8_t character,
                         const DrawArgument &args) const;

    std::unordered_map<int8_t, Texture> chars_;
    std::unordered_map<int8_t, std::string> names_;
    Alignment alignment_;
};
}  // namespace ms