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

#include <cstdint>
#include <vector>

#include "glfm.h"

namespace ms {
// One glyph rasterised by the platform text renderer, plus the metrics the layout code needs. The
// bearing/size fields mirror FreeType's bitmap_left / bitmap_top / width / rows so a platform glyph
// can stand in for a bundled-face glyph without the draw code caring which produced it.
struct GlyphBitmap {
    int32_t width = 0;
    int32_t height = 0;
    float advance = 0.0f;
    float bearing_x = 0.0f;
    float bearing_y = 0.0f;
    float ascent = 0.0f;
    // width * height coverage bytes (0..255), row-major from the top-left of the ink box.
    std::vector<uint8_t> alpha;
};

// Bridge to openMapleClient.NativeText, which draws glyphs with android.graphics over JNI. This is
// what lets text come out as sharp as in any other app and cover every code point, unlike the
// bundled FreeType faces that stop at Latin-1.
class AndroidText {
public:
    // Resolve the Java helper against the app class loader and cache the handles. Must be called
    // from the render thread (it attaches that thread to the JVM). Idempotent; returns whether the
    // bridge is usable.
    static bool init(GLFMDisplay *display);

    // Rasterize one code point at the given pixel size. Returns false for blank glyphs (e.g. a
    // space, where only out.advance is meaningful) or if the bridge is unavailable.
    static bool rasterize(uint32_t codepoint, float px_size, bool bold, GlyphBitmap &out);

    static bool available();
};
}  // namespace ms
