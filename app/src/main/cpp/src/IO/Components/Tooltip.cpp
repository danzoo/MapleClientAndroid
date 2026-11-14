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
#include "Tooltip.h"

#include <imgwz/wz.hpp>

namespace ms {
    Tooltip::Tooltip() {
        action_plate_ =
            img::wz::ui["Basic.img"]["BtOK4"]["normal"]["0"];
    }

    void Tooltip::add_action(std::string label,
                             std::function<void()> on_pressed) {
        Action action;
        action.label = std::move(label);
        action.on_pressed = std::move(on_pressed);

        // The box is written when the art goes out, and the art has not gone out yet for this
        // control. Leaving the last one standing would answer a press where nothing can be seen.
        actions_.push_back(std::move(action));
    }

    void Tooltip::clear_actions() {
        actions_.clear();
    }

    bool Tooltip::action_at(Point<int16_t> cursorpos) const {
        for (const Action &action : actions_) {
            if (action.on_pressed && action.rect.contains(cursorpos)) {
                return true;
            }
        }

        return false;
    }

    bool Tooltip::press_action(Point<int16_t> cursorpos) {
        for (Action &action : actions_) {
            if (!action.on_pressed || !action.rect.contains(cursorpos)) {
                continue;
            }

            // A copy, not the list's own: a control may ask for a preview of its own, which writes
            // this list from the front and would take the call being run out from under it. Copied
            // rather than moved so the control is still there for the next press.
            std::function<void()> on_pressed = action.on_pressed;
            on_pressed();

            return true;
        }

        return false;
    }

    int16_t Tooltip::actions_height() const {
        const int16_t count = static_cast<int16_t>(actions_.size());

        if (count == 0) {
            return 0;
        }

        // The controls stand in one row, so the foot room is a single control's height.
        return ACTION_HEIGHT + ACTION_INSET;
    }

    void Tooltip::build_actions(NativeTooltip::List &list,
                                Rectangle<int16_t> interior) const {
        if (actions_.empty() || !action_plate_.is_valid()) {
            return;
        }

        // Each control is the game's own dialog button, at the width the archive has it and grown to
        // the height of the room it stands in: the rounded corners and the outline are what make it
        // read as a button, and stretching it to the width of the panel pulls those apart into a
        // slab. The export cuts the plate to this height, so the corner art comes over whole and the
        // body alone is what grows - the word lettered into it is a line tall and would stand over
        // the canvas's own sixteen rows.
        const Point<int16_t> size(action_plate_.get_dimensions().x(),
                                  ACTION_HEIGHT);

        // The row stands in the near corner of the foot room, held off both edges it shares with the
        // border art by the same margin the lettering is held back by, so the frame stays visible all
        // the way round it. The controls sit side by side with thumb room between them, right-aligned
        // so the last one added stands exactly where a lone control stood.
        const int16_t count = static_cast<int16_t>(actions_.size());
        const int16_t row_width =
            count * size.x() + (count - 1) * ACTION_ROW_GAP;
        int16_t x = interior.right() - ACTION_INSET - row_width;
        const int16_t y = interior.bottom() - actions_height();

        for (Action &action : actions_) {
            const Point<int16_t> top_left(x, y);

            action.rect = Rectangle<int16_t>(top_left, top_left + size);

            // The plate's own lettering ("OK") is blanked out of the copy the window reads, so the
            // word shown is this one. Every control reads the same plate: the name is the art's path
            // in the archive, and the window draws one record per control at its own box.
            list.picture(action_plate_, DrawArgument(top_left, size),
                         "BtOK4/normal/0/plate");

            // The lettering is black because the plate is the dialog button's own green, whose light
            // face is what the word has to stand off.
            action.text = Text(Text::Font::A12B,
                               Text::Alignment::CENTER,
                               Color::Name::BLACK,
                               action.label,
                               size.x());

            // Centered lettering takes the origin as the middle of the line rather than its start, so
            // the pen is the middle of the plate. The window letters at the baseline, and the ink of
            // these glyphs stands mostly above it, so the word is placed by its real ink box: the box
            // centered in the plate, then the baseline recovered from the top of the box - and then
            // set down by what he asked for, because the box centered is still a word that reads too
            // high on the button's face.
            const Text::VerticalInk ink = action.text.ink_vertical();

            list.text(action.text,
                      Point<int16_t>(
                          top_left.x() + size.x() / 2,
                          top_left.y()
                              + (size.y() - (ink.bottom - ink.top)) / 2
                              - ink.top
                              + ACTION_LABEL_DROP));

            x += size.x() + ACTION_ROW_GAP;
        }
    }

    // The two ways a preview can put itself on the screen, for the previews that have not taken one
    // of them over. Text and map previews are a frame and a line or two of lettering and are still
    // drawn by the game's own pass; the item, equipment and skill previews are not, and say so.
    void Tooltip::draw(Point<int16_t> /*cursorpos*/) const {}

    bool Tooltip::build(NativeTooltip::List & /*list*/,
                        Point<int16_t> /*cursorpos*/) const {
        return false;
    }
}  // namespace ms
