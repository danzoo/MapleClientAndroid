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
#include "ItemTooltip.h"

#include <imgwz/wz.hpp>

#include "../Constants.h"
#include "../Data/ItemData.h"

namespace ms {
namespace {
// Where the three pieces of text start, measured from the left edge of the preview: the name across
// the top, the description clear of the item icon, and the quality line centered under the name.
constexpr int16_t NAME_X = 22;
constexpr int16_t DESC_X = 104;
constexpr int16_t QUALITY_X = 148;

// The icon column is this far in from the left edge of the preview.
constexpr int16_t ICON_X = 14;

// The frame is anchored at its horizontal centre this far from the left edge of the preview, so the
// content edges it leaves can be worked out before anything is placed.
constexpr int16_t FRAME_CENTER_X = 150;
}  // namespace

// TODO: Add blue dot next to name
ItemTooltip::ItemTooltip() {
    img::node Item = img::wz::ui["UIToolTip.img"]["Item"];
    img::node Frame = Item["Frame2"];
    img::node ItemIcon = Item["ItemIcon"];

    frame_ = Frame;
    cover_ = Frame["cover"];
    base_ = ItemIcon["base"];
    item_cover_ = ItemIcon["cover"];
    type_[true] = ItemIcon["new"];
    type_[false] = ItemIcon["old"];

    item_id_ = 0;
}

bool ItemTooltip::set_item(int32_t iid) {
    // Whatever control came with the last piece shown belongs to the window that asked for it, not to
    // the piece being asked for now - even the same piece, seen from another window. So it is dropped
    // before anything else, and the window that wants a control hands it over after pointing here.
    clear_action();

    if (item_id_ == iid) {
        return false;
    }

    item_id_ = iid;

    if (item_id_ == 0) {
        return false;
    }

    const ItemData &idata = ItemData::get(item_id_);

    item_icon_ = idata.get_icon(false);
    untradable_ = idata.is_untradable();
    unique_ = idata.is_unique();

    std::string quality = "";

    if (unique_ && untradable_) {
        quality = "唯一，不可交易";
    } else if (unique_ && !untradable_) {
        quality = "唯一道具";
    } else if (!unique_ && untradable_) {
        quality = "不可交易";
    } else {
        quality = "";
    }

    fill_width_ = 264;

    // The wraps come from the frame's content box, held back from its right edge by the same margin
    // the text is placed off its left. Only the horizontal edges are taken here: the frame's width
    // doesn't change with its height, which is not known until the text exists and the text cannot be
    // laid out before the edges do.
    MapleFrame::Box box = frame_.inner_box(Point<int16_t>(FRAME_CENTER_X, 1),
                                           fill_width_,
                                           1);

    int16_t edge = box.right - Tooltip::TEXT_MARGIN;
    int16_t name_w = edge - NAME_X;
    int16_t desc_w = edge - DESC_X;
    int16_t quality_w = edge - QUALITY_X;

    name_ = Text(Text::Font::A12B,
                 Text::Alignment::LEFT,
                 Color::Name::WHITE,
                 idata.get_name(),
                 name_w);
    desc_ = Text(Text::Font::A12M,
                 Text::Alignment::LEFT,
                 Color::Name::WHITE,
                 idata.get_desc(),
                 desc_w);
    qual_ = Text(Text::Font::A12M,
                 Text::Alignment::CENTER,
                 Color::Name::ORANGE,
                 quality,
                 quality_w);

    fill_height_ = 83 + name_.height();
    desc_delta_ = desc_.height() - 80;

    if (desc_delta_ > 0) {
        fill_height_ += desc_delta_;
    }

    return true;
}

bool ItemTooltip::build(NativeTooltip::List &list, Point<int16_t> pos) const {
    if (item_id_ == 0) {
        return false;
    }

    // The preview is part of the interface, so it has to be kept inside the interface's screen; the
    // same clamp the pass that used to draw it worked out, because the preview is placed against the
    // finger that opened it and has to come back inside when the finger is near an edge.
    int16_t max_width = Constants::Constants::get().get_ui_viewwidth();
    int16_t max_height = Constants::Constants::get().get_ui_viewheight();
    // The control a preview carries is set into the foot of the frame, so the frame is grown to make
    // room for it and that room counts towards what has to be kept on the screen.
    int16_t action_height = has_action() ? ACTION_HEIGHT + ACTION_INSET : 0;
    int16_t cur_width = pos.x() + fill_width_ + 32;
    int16_t cur_height = pos.y() + fill_height_ + 40 + action_height;

    int16_t adj_x = cur_width - max_width;
    int16_t adj_y = cur_height - max_height;

    int16_t adj_d = desc_delta_ > 0 ? desc_delta_ : 0;
    int16_t adj_t = (untradable_ || unique_) ? 19 : 0;

    if (adj_x > 0) {
        pos.shift_x(adj_x * -1);
    }

    if (adj_y > 0) {
        pos.shift_y(adj_y * -1);
    }

    // The frame is kept at its top edge and grown downwards, the same way a longer description grows
    // it, so the room handed to the control is room the lettering never had.
    Point<int16_t> anchor = pos + Point<int16_t>(FRAME_CENTER_X,
                                                 118 + adj_d + adj_t
                                                     + action_height);

    frame_.build(list, anchor, fill_width_,
                 fill_height_ + adj_t + action_height, "Frame2");

    MapleFrame::Box box = frame_.inner_box(anchor,
                                           fill_width_,
                                           fill_height_ + adj_t + action_height);

    list.picture(cover_, DrawArgument(pos + Point<int16_t>(4, 4)), "Frame2/cover");
    list.text(name_, pos + Point<int16_t>(NAME_X, 8));

    if (untradable_ || unique_) {
        list.text(qual_, pos + Point<int16_t>(QUALITY_X, 27));
    }

    pos.shift(ICON_X, 18 + name_.height() + adj_t);

    list.picture(base_, DrawArgument(pos), "ItemIcon/base");
    list.picture(type_[true], DrawArgument(pos), "ItemIcon/new");
    list.icon(item_icon_, DrawArgument(pos + Point<int16_t>(8, 72), 2.0f, 2.0f));
    list.picture(item_cover_, DrawArgument(pos), "ItemIcon/cover");
    list.text(desc_, pos + Point<int16_t>(DESC_X - ICON_X, -6));

    build_action(list,
                 Rectangle<int16_t>(Point<int16_t>(box.left, box.top),
                                    Point<int16_t>(box.right, box.bottom)));

    return true;
}
}  // namespace ms