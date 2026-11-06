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
#include "Charset.h"

namespace ms {
Charset::Charset(const img::node &src, Alignment alignment, const std::string &asset) :
    alignment_(alignment) {
    for (const auto &sub : src) {
        std::string name = sub.name();

        if (sub.data_type() != img::node::type::bitmap || name.empty()) {
            continue;
        }

        char c = *name.begin();

        if (c == '\\') {
            c = '/';
        }

        auto held = chars_.find(c);

        if (held != chars_.end()) {
            // Two canvases of a set can be looked up by the same letter - the requirement sets hold
            // both a percent sign and a "reqPOP" label, whose first letter is only ever reached for
            // the label. The pass that drew them kept whichever came first, so the names have to be
            // kept with it; a set that recorded the second one's canvas against the first one's
            // file would draw the wrong picture in the wrong place.
            continue;
        }

        chars_.emplace(c, sub);
        names_.emplace(c, asset + "/" + name);
    }
}

Charset::Charset() : alignment_(Charset::Alignment::LEFT) {}

void Charset::draw(int8_t c, const DrawArgument &args) const {
    auto iter = chars_.find(c);

    if (iter != chars_.end()) {
        iter->second.draw(args);
    }
}

void Charset::build_character(NativeTooltip::List &list,
                              int8_t c,
                              const DrawArgument &args) const {
    auto held = chars_.find(c);

    if (held == chars_.end()) {
        return;
    }

    auto name = names_.find(c);

    if (name != names_.end()) {
        list.picture(held->second, args, name->second);
    }
}

int16_t Charset::build(NativeTooltip::List &list,
                       const std::string &text,
                       int16_t hspace,
                       const DrawArgument &args) const {
    int16_t shift = 0;

    switch (alignment_) {
        case Charset::Alignment::CENTER:
            // The pass this mirrors counted the run and drew none of it, because that is what the
            // window has always looked like. No preview asks for a centered set.
            shift -= hspace * static_cast<int16_t>(text.size()) / 2;
            break;
        case Charset::Alignment::LEFT:
            for (char c : text) {
                build_character(list, c, args + Point<int16_t>(shift, 0));
                shift += hspace;
            }

            break;
        case Charset::Alignment::RIGHT:
            for (auto iter = text.rbegin(); iter != text.rend(); ++iter) {
                char c = *iter;
                shift += hspace;
                build_character(list, c, args - Point<int16_t>(shift, 0));
            }

            break;
    }

    return shift;
}

int16_t Charset::getw(int8_t c) const {
    auto iter = chars_.find(c);

    return iter != chars_.end() ? iter->second.width() : 0;
}

int16_t Charset::draw(const std::string &text, const DrawArgument &args) const {
    int16_t shift = 0;
    int16_t total = 0;

    switch (alignment_) {
        case Charset::Alignment::CENTER:
            for (char c : text) {
                int16_t width = getw(c);

                draw(c, args + Point<int16_t>(shift, 0));
                shift += width + 2;
                total += width;
            }

            shift -= total / 2;
            break;
        case Charset::Alignment::LEFT:
            for (char c : text) {
                draw(c, args + Point<int16_t>(shift, 0));
                shift += getw(c) + 1;
            }

            break;
        case Charset::Alignment::RIGHT:
            for (auto iter = text.rbegin(); iter != text.rend(); ++iter) {
                char c = *iter;
                shift += getw(c);
                draw(c, args - Point<int16_t>(shift, 0));
            }

            break;
    }

    return shift;
}

int16_t Charset::draw(const std::string &text,
                      int16_t hspace,
                      const DrawArgument &args) const {
    size_t length = text.size();
    int16_t shift = 0;

    switch (alignment_) {
        case Charset::Alignment::CENTER:
            shift -= hspace * static_cast<int16_t>(length) / 2;
            break;
        case Charset::Alignment::LEFT:
            for (char c : text) {
                draw(c, args + Point<int16_t>(shift, 0));
                shift += hspace;
            }

            break;
        case Charset::Alignment::RIGHT:
            for (auto iter = text.rbegin(); iter != text.rend(); ++iter) {
                char c = *iter;
                shift += hspace;
                draw(c, args - Point<int16_t>(shift, 0));
            }

            break;
    }

    return shift;
}
}  // namespace ms