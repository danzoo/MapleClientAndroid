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

#include <functional>
#include <list>
#include <memory>

#include "../Components/Icon.h"
#include "../Template/EnumMap.h"
#include "Components/Tooltip.h"
#include "Keyboard.h"
#include "UIElement.h"

namespace ms {
class UIState {
public:
    using Iterator = EnumMap<UIElement::Type,
                             std::unique_ptr<UIElement>,
                             UIElement::Type::NUM_TYPES>::iterator;

    virtual ~UIState() = default;

    virtual void draw(float inter, Point<int16_t> cursor) const = 0;

    virtual void update() = 0;

    virtual void doubleclick(Point<int16_t> pos) = 0;

    virtual void rightclick(Point<int16_t> pos) = 0;

    virtual void send_key(KeyType::Id type,
                          int32_t action,
                          bool pressed,
                          bool escape) = 0;

    virtual Cursor::State send_cursor(Cursor::State mst,
                                      Point<int16_t> pos) = 0;

    virtual bool cursor_in_drag_area(Point<int16_t> pos) {
        return false;
    }

    virtual void send_scroll(double yoffset) = 0;

    virtual void send_close() = 0;

    virtual void drag_icon(Icon *icon) = 0;

    // A gesture the system took away (a swipe-up home gesture stealing the touch stream, a shade
    // pulled down mid-drag) ends without a release the player meant. Whatever was picked up goes
    // back rather than staying glued to a finger that is no longer there.
    virtual void cancel_drag() {}

    virtual void clear_tooltip(Tooltip::Parent parent) = 0;

    // Clear whichever tooltip is currently raised, whatever its parent. Used by the touch layer
    // when a scroll gesture begins and any item preview must vanish.
    virtual void clear_tooltip() {}

    // Whether a preview stands right now that one of the given window's pieces raised. The shop reads
    // this to tell a second click on a row from a fresh one, because the preview - not the row - is
    // what says the piece has just been looked at: any gesture that puts the preview down, including
    // ones this window never sees, takes the row back to needing a look first.
    virtual bool has_tooltip(Tooltip::Parent parent) const { return false; }

    // A preview stands at an anchor rather than at the finger: the window that raises one hands
    // over the centre of the piece that was touched, so that where inside it the finger landed
    // changes nothing about where the words appear. Passing the cursor itself is what a preview
    // with no piece of art behind it - a place on the minimap, a line of plain text - still does.
    virtual void show_equip(Tooltip::Parent parent,
                            int16_t slot,
                            Point<int16_t> anchor) = 0;

    virtual void show_item(Tooltip::Parent parent,
                           int32_t itemid,
                           Point<int16_t> anchor) = 0;

    // A preview of something for sale rather than something owned: there is no slot of the
    // character's to read it from, only its id. Only the game state has previews that can hold one.
    virtual void show_goods(Tooltip::Parent parent,
                            int32_t item_id,
                            Point<int16_t> anchor) {}

    // The one control a raised preview carries, put on after the preview has been asked for. Setting
    // it does not rebuild anything by itself; the window asks for the preview afresh when it wants
    // the control to appear.
    virtual void set_tooltip_action(std::string label,
                                    std::function<void()> on_pressed) {}

    virtual void show_skill(Tooltip::Parent parent,
                            int32_t skill_id,
                            int32_t level,
                            int32_t masterlevel,
                            int64_t expiration,
                            Point<int16_t> anchor) = 0;

    virtual void show_text(Tooltip::Parent parent,
                           std::string text,
                           Point<int16_t> anchor) = 0;

    virtual void show_map(Tooltip::Parent parent,
                          std::string name,
                          std::string description,
                          int32_t mapid,
                          bool bolded,
                          Point<int16_t> anchor) = 0;

    virtual Iterator pre_add(UIElement::Type type,
                             bool toggled,
                             bool focused) = 0;

    virtual void remove(UIElement::Type type) = 0;

    virtual UIElement *get(UIElement::Type type) = 0;

    virtual UIElement *get_front(std::list<UIElement::Type> types) = 0;

    virtual UIElement *get_front(Point<int16_t> pos) = 0;
};

class UIStateNull : public UIState {
    void draw(float, Point<int16_t>) const override {}
    void update() override {}
    void doubleclick(Point<int16_t>) override {}
    void rightclick(Point<int16_t>) override {}
    void send_key(KeyType::Id, int32_t, bool, bool) override {}
    Cursor::State send_cursor(Cursor::State, Point<int16_t>) override {
        return Cursor::State::IDLE;
    }
    void send_scroll(double) override {}
    void send_close() override {}
    void drag_icon(Icon *) override {}
    void clear_tooltip(Tooltip::Parent) override {}
    void show_equip(Tooltip::Parent, int16_t, Point<int16_t>) override {}
    void show_item(Tooltip::Parent, int32_t, Point<int16_t>) override {}
    void show_skill(Tooltip::Parent,
                    int32_t,
                    int32_t,
                    int32_t,
                    int64_t,
                    Point<int16_t>) override {}
    void show_text(Tooltip::Parent, std::string, Point<int16_t>) override {}
    void show_map(Tooltip::Parent,
                  std::string,
                  std::string,
                  int32_t,
                  bool,
                  Point<int16_t>) override {}
    Iterator pre_add(UIElement::Type, bool, bool) override {
        return { nullptr, UIElement::Type::NUM_TYPES };
    }
    void remove(UIElement::Type) override {}
    UIElement *get(UIElement::Type) override { return nullptr; }
    UIElement *get_front(std::list<UIElement::Type>) override {
        return nullptr;
    }
    UIElement *get_front(Point<int16_t>) override { return nullptr; }
};
}  // namespace ms