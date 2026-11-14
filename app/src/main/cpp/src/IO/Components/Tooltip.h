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
#include <functional>
#include <string>
#include <vector>

#include "../Graphics/Text.h"
#include "../Graphics/Texture.h"
#include "../Template/Point.h"
#include "../Template/Rectangle.h"
#include "../NativeTooltip.h"

namespace ms {
// Interface for tooltips, information windows about something
// the mouse cursor is pointed at.
class Tooltip {
public:
    // How far text stands off the edge it is kept inside of: enough that a line which does use its
    // entire wrap still finishes short of the border art.
    static constexpr int16_t TEXT_MARGIN = 8;

    // The height of one control a preview carries, how far the whole column of them stands off the
    // two edges it is kept clear of, and the room between two controls stacked one over the other.
    static constexpr int16_t ACTION_HEIGHT = 22;
    static constexpr int16_t ACTION_INSET = 8;
    static constexpr int16_t ACTION_GAP = 4;
    // Controls sit in a row, so the gap between them is thumb room rather than line spacing.
    static constexpr int16_t ACTION_ROW_GAP = 8;

    // How far the control's word is set below the middle of its plate. Centering the ink box leaves
    // the lettering reading high on the button's face, whose own highlight sits along the top.
    static constexpr int16_t ACTION_LABEL_DROP = 1;

    // Possible parent UIs for Tooltips.
    enum Parent {
        NONE,
        EQUIP_INVENTORY,
        ITEM_INVENTORY,
        SKILL_BOOK,
        SHOP,
        EVENT,
        TEXT,
        KEY_CONFIG,
        GAMEPAD_CONFIG,
        WORLD_MAP,
        MINI_MAP
    };

    Tooltip();

    virtual ~Tooltip() = default;

    // Adds one control to the column a preview carries, below the ones already added. The window the
    // preview is put on lets touches fall straight through to the game, so a press on a control never
    // arrives as the preview's own: the box it was written into is remembered as the art goes out, and
    // the press is read against that. Only the window that raised the preview knows what the press
    // should mean, so only it can say what a control does.
    void add_action(std::string label, std::function<void()> on_pressed);

    // Takes the whole column off again. A preview is written from the front every time it is pointed
    // at a new piece, so a control never outlives the piece it was put up for.
    void clear_actions();

    // Whether the press lands on one of the controls. A touch down there belongs to that control and
    // to nothing else: the window behind the preview must not read it as a tap on its own face, which
    // for a list is how the preview comes back down before the press that would have used it.
    bool action_at(Point<int16_t> cursorpos) const;

    // Runs the control the press lands on, and reports whether one of them owned the press.
    bool press_action(Point<int16_t> cursorpos);

    // Painting the preview into the game's own pass. A preview that the native window has taken over
    // has nothing to draw here and leaves it alone.
    virtual void draw(Point<int16_t> cursorpos) const;

    // The same preview written out for the native window: the pieces it is made of, in the order
    // they would have been drawn, in the units the interface lays itself out in. A preview that has
    // not been converted says so by returning false, and is drawn by the pass above as it always
    // was.
    virtual bool build(NativeTooltip::List &list, Point<int16_t> cursorpos) const;

protected:
    // The room the column of controls takes at the foot of a panel, which a panel has to add to its
    // own height before it lays anything out. Nothing is reserved for a preview that carries none.
    int16_t actions_height() const;

    // Writes the controls into the foot of a panel whose inside is `interior`, and remembers the box
    // each landed in. Writing and remembering are one call because the press has to be read against
    // the very box the reader was shown. The panel is grown by actions_height() before this, so the
    // controls take room that lettering does not use.
    void build_actions(NativeTooltip::List &list,
                       Rectangle<int16_t> interior) const;

private:
    // One control: the word on it, what pressing it means, and the box it was written into. The box
    // and the lettering are decided when the art goes out, which is the only pass that knows where
    // the panel ended up after the screen held it back.
    struct Action {
        std::string label;
        std::function<void()> on_pressed;
        Rectangle<int16_t> rect;
        Text text;
    };

    // The plate every control is drawn from. Taken up once, in the constructor, because making a
    // Texture registers its pixels with the atlas and a preview is written many times a minute.
    Texture action_plate_;

    mutable std::vector<Action> actions_;
};
}  // namespace ms