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
#include "SkillTooltip.h"

#include <imgwz/wz.hpp>

#include "../Data/SkillData.h"

namespace ms {
namespace {
// The description sits right of the skill icon rather than inside the text margin, so it has its own
// left edge and only shares the right one.
constexpr int16_t DESC_OFFSET = 73;

// The frame is anchored at its horizontal centre this far from the left edge of the preview, so the
// content edges it leaves can be worked out before anything is placed.
constexpr int16_t FRAME_CENTER_X = 176;
}  // namespace

SkillTooltip::SkillTooltip() {
    img::node Frame = img::wz::ui["UIToolTip.img"]["Item"]["Frame2"];

    frame_ = Frame;
    cover_ = Frame["cover"];

    skill_id_ = 0;
}

void SkillTooltip::set_skill(int32_t id,
                             int32_t level,
                             int32_t mlevel,
                             int64_t expiration) {
    if (skill_id_ == id) {
        return;
    }

    skill_id_ = id;

    if (skill_id_ == 0) {
        return;
    }

    const SkillData &data = SkillData::get(id);

    int32_t masterlevel;

    if (mlevel > 0) {
        masterlevel = mlevel;
    } else {
        masterlevel = data.get_masterlevel();
    }

    std::string descstr = data.get_desc();

    if (masterlevel > 0) {
        // The archive writes this tag itself, in Chinese, but with the number as published rather
        // than the number this skill really has - so the number is replaced in place and no second
        // tag is added beside it. The colon comes in both a full-width and a half-width form in the
        // files, so the number is found by scanning for digits inside the bracket instead of after
        // one particular colon.
        const std::string mltag = "最高等级";
        const std::string mlstr = std::to_string(masterlevel);
        const std::string digits = "0123456789";

        size_t mlstart = descstr.find(mltag);
        size_t mlclose = descstr.find(']', mlstart);
        size_t mlpos =
            descstr.find_first_of(digits, mlstart + mltag.size());

        if (mlpos == std::string::npos || mlclose == std::string::npos
            || mlpos >= mlclose) {
            descstr.insert(0, "[" + mltag + " : " + mlstr + "]\\n");
        } else {
            size_t mlend = descstr.find_first_not_of(digits, mlpos);
            descstr.replace(mlpos, mlend - mlpos, mlstr);
        }
    }

    const std::string exptag = "#cAvailable until";

    if (expiration > 0) {
        // TODO: Blank
    } else {
        size_t expstart = descstr.find(exptag);
        size_t expend = descstr.find('#', expstart + 1);

        if (expstart < expend && expend != std::string::npos) {
            size_t expsize = expend - expstart + 1;
            descstr.erase(expstart, expsize);
        }
    }

    std::string levelstr;
    bool current = level > 0;
    bool next = level < masterlevel;

    if (current) {
        levelstr += "[当前等级 : " + std::to_string(level) + "]\\n"
                    + data.get_level_desc(level);
    }

    if (current && next) {
        levelstr += "\\n";
    }

    if (next) {
        levelstr += "[下一等级 : " + std::to_string(level + 1) + "]\\n"
                    + data.get_level_desc(level + 1);
    }

    width_ = 292;

    // Only the horizontal edges are taken from this. The vertical ones depend on how tall the text
    // turns out, which is not known until the text exists - and the text cannot be laid out until
    // the horizontal edges are known. The frame's width does not change with its height, so the
    // edges come out the same whatever height is passed.
    MapleFrame::Box box = frame_.inner_box(Point<int16_t>(FRAME_CENTER_X, 1),
                                           width_,
                                           1);

    icon_ = data.get_icon(SkillData::Icon::NORMAL);

    text_x_ = box.left + Tooltip::TEXT_MARGIN;

    // The tile is the art magnified twice with two units of padding on each side, so where it ends
    // depends on the icon rather than on the frame.
    int16_t icon_width = (icon_.get_dimensions().x() * 2) + 4;

    // The description column is the tile's neighbour on the right and has to start clear of it. The
    // fixed offset was only ever right while the tile sat left of the text edge; once the tile moved
    // onto that edge its last units ran over the first glyph of every line in the column.
    desc_x_ = std::max(box.left + DESC_OFFSET, text_x_ + icon_width + 8);

    int16_t edge = box.right - Tooltip::TEXT_MARGIN;
    int16_t text_w = edge - text_x_;
    int16_t desc_w = edge - desc_x_;

    name_ = Text(Text::Font::A12B,
                 Text::Alignment::LEFT,
                 Color::Name::WHITE,
                 data.get_name(),
                 text_w);
    desc_ = Text(Text::Font::A12M,
                 Text::Alignment::LEFT,
                 Color::Name::WHITE,
                 descstr,
                 desc_w);
    level_desc_ = Text(Text::Font::A12M,
                       Text::Alignment::LEFT,
                       Color::Name::WHITE,
                       levelstr,
                       text_w);

    int16_t desc_height = desc_.height() + 11;

    icon_offset_ = name_.height();
    level_offset_ = std::max<int16_t>(desc_height, 85);
    height_ = icon_offset_ + level_offset_ + level_desc_.height();

    line_ = ColorLine(box.right - Tooltip::TEXT_MARGIN - text_x_,
                      Color::Name::WHITE,
                      1.0f);
    box_ = ColorBox(icon_width, icon_width, Color::Name::WHITE, 0.65f);
}

bool SkillTooltip::build(NativeTooltip::List &list, Point<int16_t> pos) const {
    if (skill_id_ == 0) {
        return false;
    }

    // The same clamp the pass that used to draw this worked out, for the same reason: the preview is
    // placed against the finger that opened it and has to come back inside the interface's screen
    // when the finger is near an edge.
    int16_t max_width = Constants::Constants::get().get_ui_viewwidth();
    int16_t max_height = Constants::Constants::get().get_ui_viewheight();
    int16_t cur_width = pos.x() + width_ + 45;
    int16_t cur_height = pos.y() + height_ + 35;

    int16_t adj_x = cur_width - max_width;
    int16_t adj_y = cur_height - max_height;

    if (adj_x > 0) {
        pos.shift_x(adj_x * -1);
    }

    if (adj_y > 0) {
        pos.shift_y(adj_y * -1);
    }

    frame_.build(list,
                 pos + Point<int16_t>(FRAME_CENTER_X, height_ + 11),
                 width_,
                 height_ - 1,
                 "Frame2");
    list.text(name_, pos + Point<int16_t>(text_x_, 3));
    list.picture(cover_, DrawArgument(pos + Point<int16_t>(16, -1)), "Frame2/cover");

    pos.shift_y(icon_offset_);

    // The tile is the icon's visible left edge, so it is set to the same one the text under it is
    // laid out against. The art sits two units inside it, because the tile is two units wider than
    // the art on each side.
    list.block(pos + Point<int16_t>(text_x_, 21),
               Point<int16_t>(box_.width(), box_.height()),
               box_.color(),
               box_.opacity());
    list.icon(icon_,
              DrawArgument(pos + Point<int16_t>(text_x_ + 2, 87), 2.0f, 2.0f));
    list.text(desc_, pos + Point<int16_t>(desc_x_, 15));

    pos.shift_y(level_offset_);

    list.block(pos + Point<int16_t>(text_x_, 10),
               Point<int16_t>(line_.width(), 1),
               line_.color(),
               line_.opacity());
    list.text(level_desc_, pos + Point<int16_t>(text_x_, 11));

    return true;
}
}  // namespace ms