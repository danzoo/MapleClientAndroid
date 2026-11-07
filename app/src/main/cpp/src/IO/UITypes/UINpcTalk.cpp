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
#include "UINpcTalk.h"

#include <imgwz/wz.hpp>

#include <algorithm>

#include "../Components/MapleButton.h"
#include "../Constants.h"
#include "../Data/ItemData.h"
#include "../Gameplay/Stage.h"
#include "../Net/Packets/NpcInteractionPackets.h"
#include "../UI.h"
#include "GameInfo.h"
#include <string_view>
#include <regex>

namespace ms {
    namespace {
        auto fn_npc_talk_more = []<typename... T>(T &&...args) {
            NpcTalkMorePacket(std::forward<T>(args)...).dispatch();
        };

        // Where the text is after one more thing that draws.
        //
        // The words are let out a character at a time, but the marks threaded between them are not
        // characters: the layout eats them to pick a colour or break a line and never letters them. So
        // a step takes any marks that come next and then one drawn thing, which is what keeps the
        // reader from ever seeing half of a mark - a prefix ending on `#k`'s `#` would letter that
        // `k` as a word.
        size_t next_word(const std::string &text, size_t pos) {
            while (pos < text.size()) {
                const char c = text[pos];

                if (c == '\\') {
                    // Only the two the layout answers to are marks; a backslash standing before
                    // anything else is just a backslash, and is drawn.
                    if (pos + 1 < text.size()
                        && (text[pos + 1] == 'n' || text[pos + 1] == 'r')) {
                        return pos + 2;
                    }

                    break;
                }

                if (c == '#') {
                    if (pos + 1 >= text.size()) {
                        return pos + 1;
                    }

                    const char mark = text[pos + 1];

                    if (mark == 'L') {
                        // An answer's opener comes whole: the number it carries belongs to the mark,
                        // and the layout letters none of it, so showing part of it would be a step
                        // that put nothing on the screen.
                        size_t next = pos + 2;

                        while (next < text.size()
                               && text[next] >= '0' && text[next] <= '9') {
                            next++;
                        }

                        if (next < text.size() && text[next] == '#') {
                            next++;
                        }

                        return next;
                    }

                    if (mark == 'k' || mark == 'b' || mark == 'r' || mark == 'c'
                        || mark == 'l') {
                        return pos + 2;
                    }

                    // A bare hash is swallowed on its own.
                    return pos + 1;
                }

                if (static_cast<unsigned char>(c) < 32) {
                    // The breaks the text is split on for the buttons. The layout measures them as
                    // nothing, so they are not owed any of the time a character is.
                    return pos + 1;
                }

                break;
            }

            if (pos >= text.size()) {
                return pos;
            }

            // One sequence of a multi-byte alphabet counts as the character it letters as. Clamped
            // so that a text cut short in the middle of one still ends on its last byte.
            const auto lead = static_cast<unsigned char>(text[pos]);
            size_t bytes = 1;

            if ((lead & 0xE0) == 0xC0) {
                bytes = 2;
            } else if ((lead & 0xF0) == 0xE0) {
                bytes = 3;
            } else if ((lead & 0xF8) == 0xF0) {
                bytes = 4;
            }

            return std::min(pos + bytes, text.size());
        }

        // The pair a packet breaks its lines with is folded into the one character the reading keeps.
        // Every offset carried with the words points into them as they are stored, so a carriage
        // return left in front of a line feed would put the two a byte apart for ever.
        void normalize_breaks(std::string &text) {
            std::string folded;
            folded.reserve(text.size());

            for (size_t i = 0; i < text.size(); i++) {
                if (text[i] != '\r') {
                    folded += text[i];
                    continue;
                }

                folded += '\n';

                if (i + 1 < text.size() && text[i + 1] == '\n') {
                    i++;
                }
            }

            text = std::move(folded);
        }
    }  // namespace

    UINpcTalk::Conversation UINpcTalk::conversation_;

    UINpcTalk::UINpcTalk() :
            native_shown_(false),
            native_dirty_(true),
            unit_rows_(1),
            row_max_(0),
            show_slider_(false) {
        img::node UIWindow2 = img::wz::ui["UIWindow2.img"];
        img::node UtilDlgEx = UIWindow2["UtilDlgEx"];

        top_ = UtilDlgEx["t"];
        fill_ = UtilDlgEx["c"];
        bottom_ = UtilDlgEx["s"];
        nametag_ = UtilDlgEx["bar"];

        // A native window is handed its chrome by name rather than by pixels, so each button is
        // recorded together with the path it was read from. Doing both in one call is the point: the
        // name belongs to the button standing in that slot, and the two would come apart the first
        // time one of them was moved.
        auto button = [&](uint16_t id, const img::node &src, const char *asset) {
            buttons_[id] = std::make_unique<MapleButton>(src);
            button_assets_[id] = asset;
        };

        button(Buttons::ALL_LEVEL, UtilDlgEx["BtAllLevel"], "UtilDlgEx/BtAllLevel");
        button(Buttons::CLOSE, UtilDlgEx["BtClose"], "UtilDlgEx/BtClose");
        button(Buttons::MY_LEVEL, UtilDlgEx["BtMyLevel"], "UtilDlgEx/BtMyLevel");

        // TODO: Replace when _inlink is fixed
        // button(Buttons::NO, UtilDlgEx["BtNo"], "UtilDlgEx/BtNo");

        img::node Quest = UIWindow2["Quest"];

        button(Buttons::NO, Quest["BtNo"], "Quest/BtNo");
        button(Buttons::OK, UtilDlgEx["BtOK"], "UtilDlgEx/BtOK");
        button(Buttons::QAFTER, UtilDlgEx["BtQAfter"], "UtilDlgEx/BtQAfter");
        button(Buttons::QCNO, UtilDlgEx["BtQCNo"], "UtilDlgEx/BtQCNo");
        button(Buttons::QCYES, UtilDlgEx["BtQCYes"], "UtilDlgEx/BtQCYes");
        button(Buttons::QGIVEUP, UtilDlgEx["BtQGiveup"], "UtilDlgEx/BtQGiveup");
        button(Buttons::QNO, UtilDlgEx["BtQNo"], "UtilDlgEx/BtQNo");
        button(Buttons::QSTART, UtilDlgEx["BtQStart"], "UtilDlgEx/BtQStart");
        button(Buttons::QYES, UtilDlgEx["BtQYes"], "UtilDlgEx/BtQYes");
        button(Buttons::YES, UtilDlgEx["BtYes"], "UtilDlgEx/BtYes");

        name_ = Text(Text::Font::A11M, Text::Alignment::CENTER, Color::Name::WHITE);

        onmoved_ = [&](bool upwards) {
            const int16_t shift = upwards ? -unit_rows_ : unit_rows_;
            const bool above = conversation_.scroll_ + shift >= 0;
            const bool below = conversation_.scroll_ + shift <= row_max_ - unit_rows_;

            if (above && below) {
                conversation_.scroll_ += shift;

                // Taking the bar in hand is the reader's own view of the conversation, and the words
                // have no claim on it until the character is spoken to again. The lines keep their
                // places in the words; only where the words are looked at from moves.
                conversation_.follow_ = false;

                // Scrolling moves the words, and the words are what the window is showing.
                native_dirty_ = true;
            }
        };

        UI::get().remove_textfield();
    }

    void UINpcTalk::draw(float inter) const {
        // Read once per change, not once per frame: the window is handed a whole dialog and keeps it,
        // so rebuilding a list the screen already shows would only cost the crossing.
        if (native_dirty_) {
            NativeNpcTalk::List list;
            const bool handed_over = NativeNpcTalk::available() && build(list);

            // Only touch the window when there is something to put up, or when a dialog of ours is
            // still standing there. A dialog that could not be handed over whole keeps being painted
            // by this pass, so the records built before the piece that failed must not be left up
            // behind it.
            if (handed_over) {
                NativeNpcTalk::show(list);
            }
            else if (native_shown_) {
                NativeNpcTalk::show(NativeNpcTalk::List());
            }

            native_shown_ = handed_over;
            native_dirty_ = false;
        }

        if (native_shown_) {
            return;
        }

        Point<int16_t> drawpos = position_;
        top_.draw(drawpos);
        drawpos.shift_y(top_.height());
        fill_.draw(DrawArgument(drawpos, Point<int16_t>(0, height_)));
        drawpos.shift_y(height_);
        bottom_.draw(drawpos);
        drawpos.shift_y(bottom_.height());

        UIElement::draw(inter);

        int16_t speaker_y = (top_.height() + height_ + bottom_.height()) / 2;
        Point<int16_t> speaker_pos = position_ + Point<int16_t>(22, 11 + speaker_y);
        Point<int16_t> center_pos =
                speaker_pos + Point<int16_t>(nametag_.width() / 2, 0);

        speaker_.draw(DrawArgument(center_pos, true));
        nametag_.draw(speaker_pos);

        // The bar is art with a fixed height, so the name has to be placed by where its ink
        // actually lands. The layout box's synthetic line spacing puts CJK ink several pixels
        // higher than latin ink, which a single hand-tuned offset can't centre for both.
        const Text::VerticalInk name_ink = name_.ink_vertical();
        const int16_t bar_mid = speaker_pos.y() + nametag_.height() / 2;
        const int16_t name_y = bar_mid - (name_ink.top + name_ink.bottom) / 2;

        name_.draw(Point<int16_t>(center_pos.x(), name_y));

        // The lines are put up in the order they are read, and the view is looking at them from
        // wherever it was taken. Where each stands is worked out once, when they are put up.
        const Point<int16_t> shift = scroll_shift();

        if (show_slider_) {
            const int16_t text_min_height = position_.y() + top_.height() - 1;
            const Range<int16_t> band(text_min_height, text_min_height + height_);

            for (const auto &row: content_.rows_) {
                row.text_.draw(position_ + row.position_ + shift, band);
            }

            slider_.draw(position_);
        } else {
            for (const auto &row: content_.rows_) {
                row.text_.draw(position_ + row.position_ + shift);
            }
        }
    }

    bool UINpcTalk::build(NativeNpcTalk::List &list) const {
        // The frame is three bands: a head, a body of whatever height the words need, and a foot. The
        // body is one canvas pulled to that height rather than a row of them, which is why it is
        // handed a stretch instead of a position.
        Point<int16_t> body = position_;
        list.picture(top_, DrawArgument(body), "UtilDlgEx/t");
        body.shift_y(top_.height());
        list.picture(fill_, DrawArgument(body, Point<int16_t>(0, height_)), "UtilDlgEx/c");
        body.shift_y(height_);
        list.picture(bottom_, DrawArgument(body), "UtilDlgEx/s");

        // The buttons are inside the frame's own box, so a panel that only carried the frame would
        // cover them; they have to travel over with it.
        for (const auto &entry: button_assets_) {
            auto stored = buttons_.find(entry.first);

            if (stored == buttons_.end() || !stored->second->is_active()) {
                continue;
            }

            // Only an id this class put into the table is in the table, and every one of those was
            // made here as a MapleButton.
            const auto &frame = static_cast<const MapleButton &>(*stored->second);

            if (!frame.build(list, position_, entry.second)) {
                return false;
            }
        }

        const int16_t speaker_y = (top_.height() + height_ + bottom_.height()) / 2;
        const Point<int16_t> speaker_pos =
                position_ + Point<int16_t>(22, 11 + speaker_y);
        const Point<int16_t> center_pos =
                speaker_pos + Point<int16_t>(nametag_.width() / 2, 0);

        // The character being spoken by is the one piece not shared by every dialog: it lives in an
        // archive the platform cannot open, so it crosses as pixels. It is drawn facing its own
        // words, which is the mirroring this pass asked for with a negative scale.
        list.icon(speaker_, DrawArgument(center_pos, true));
        list.picture(nametag_, DrawArgument(speaker_pos), "UtilDlgEx/bar");

        // The name sits on the middle of the bar measured by where its ink actually lands, not by the
        // line box - the same reason the pass needed the ink.
        const Text::VerticalInk name_ink = name_.ink_vertical();
        const int16_t bar_mid = speaker_pos.y() + nametag_.height() / 2;
        const int16_t name_y = bar_mid - (name_ink.top + name_ink.bottom) / 2;

        list.text(name_, Point<int16_t>(center_pos.x(), name_y));

        // The lines travel in the order they are read, at the same places this pass would have put
        // them - which is the same arithmetic the clicking uses, so the two cannot come apart.
        const Point<int16_t> shift = scroll_shift();

        if (show_slider_) {
            const int16_t text_min_height = position_.y() + top_.height() - 1;
            const Range<int16_t> band(text_min_height, text_min_height + height_);

            for (const auto &row: content_.rows_) {
                list.text(row.text_, position_ + row.position_ + shift, band);
            }

            slider_.build(list, position_);

            return true;
        }

        for (const auto &row: content_.rows_) {
            list.text(row.text_, position_ + row.position_ + shift);
        }

        return true;
    }

    void UINpcTalk::hide_native() const {
        if (native_shown_) {
            NativeNpcTalk::show(NativeNpcTalk::List());
            native_shown_ = false;
        }

        native_dirty_ = false;
    }

    void UINpcTalk::dismiss() {
        UI::get().remove(TYPE);
        NativeNpcTalk::dismiss();
    }

    void UINpcTalk::update() {
        UIElement::update();

        if (answering_) {
            // The dialog is being kept up only to cover the answer's round trip. A page that comes
            // back replaces this object before the wait runs out; nothing coming back is a
            // conversation that is done, and the wait running out is what takes the dialog down.
            answer_ms_ += Constants::TIMESTEP;

            if (answer_ms_ >= Constants::NPC_TALK_ANSWER_MS) {
                answering_ = false;
                deactivate();
                hide_native();
            }

            return;
        }

        if (typing()) {
            // An update is Constants::TIMESTEP of real time, so the milliseconds are kept against that
            // and spent a character at a time. Keeping them as time rather than as a count of updates
            // is what stops a retuned timestep from quietly changing how fast a character talks.
            conversation_.typed_ += Constants::TIMESTEP;

            bool said = false;

            while (typing()
                   && conversation_.typed_ >= Constants::NPC_TALK_MS_PER_CHAR) {
                conversation_.typed_ -= Constants::NPC_TALK_MS_PER_CHAR;
                conversation_.revealed_ =
                        next_word(conversation_.text_, conversation_.revealed_);
                said = true;
            }

            if (said) {
                reveal();
                native_dirty_ = true;
            }
        }

        if (!typing() && conversation_.paging_ && !conversation_.awaiting_) {
            // A page that has been said in full is left up for a moment before the character is asked
            // for the next, so that the reader is not walked past the last line the moment it lands.
            // What is sent is the answer the button that used to turn the page sent, and the dialog is
            // put up as it was while the answer is in flight - which is what lets the next page join
            // the words on the screen instead of arriving over a blanked one.
            conversation_.held_ += Constants::TIMESTEP;

            if (conversation_.held_ >= Constants::NPC_TALK_PAGE_MS) {
                conversation_.awaiting_ = true;
                fn_npc_talk_more(type_, 1);
            }
        }
    }

    bool UINpcTalk::typing() const {
        return conversation_.revealed_ < conversation_.text_.size();
    }

    void UINpcTalk::reveal() {
        content_ = content_for(conversation_.revealed_);

        reset_content_pos();
        follow_caret();
    }

    Point<int16_t> UINpcTalk::scroll_shift() const {
        if (!show_slider_) {
            return Point<int16_t>();
        }

        return Point<int16_t>(0, -conversation_.scroll_ * SCROLL_ROW_);
    }

    void UINpcTalk::follow_caret() {
        if (!show_slider_ || !conversation_.follow_) {
            return;
        }

        // Where the last line said lands, measured the way the drawing measures it: the words start
        // this far below the top of the frame and every line is worth its own layout height.
        auto content_bottom = [this]() {
            int32_t bottom = 19;

            for (const auto &row: content_.rows_) {
                bottom += row.text_.height();
            }

            return bottom;
        };

        // The band the words are read through, in the same coordinates as the bottom above.
        const int32_t visible = top_.height() - 1 + height_;

        while (conversation_.scroll_ < row_max_ - unit_rows_
               && content_bottom() - conversation_.scroll_ * SCROLL_ROW_ > visible) {
            conversation_.scroll_++;
        }

        // The bar is what the reader sees of where they are in the words, so it has to be told too.
        slider_.setrows(conversation_.scroll_, unit_rows_, row_max_);
    }

    void UINpcTalk::Conversation::begin(int32_t npcid, std::string said) {
        npcid_ = npcid;
        text_ = std::move(said);
        lines_.clear();
        revealed_ = 0;
        typed_ = 0;
        held_ = 0;
        awaiting_ = false;
        anchored_ = false;
        scroll_ = 0;
        follow_ = true;
    }

    Button::State UINpcTalk::button_pressed(uint16_t buttonid) {
        // An answer is already on its way; only an explicit close is still answered.
        if (answering_ && buttonid != Buttons::CLOSE) {
            return Button::State::NORMAL;
        }

        // Whatever the answer is, the conversation as far as it had run out is done with: the reader
        // has turned to something that is not another page of what the character was saying.
        conversation_.paging_ = false;
        conversation_.awaiting_ = false;

        if (buttonid == Buttons::CLOSE) {
            deactivate();

            // An answer is sent and this dialog is done with, but the window is not skipped along with
            // the pass - nothing reaches it once the game lets go - so the words have to be taken off by
            // asking.
            hide_native();
        } else {
            // The words stay up while the answer is in flight. A page that comes back replaces them
            // without a blank frame; if the conversation is done instead, the wait in update runs out
            // and takes them down. Blanking here is what made a continuing dialog flash once between
            // the press and the reply.
            answering_ = true;
            answer_ms_ = 0;
        }

        switch (type_) {
            case TalkType::SEND_NEXT:
            case TalkType::SEND_OK:
                // Type = 0
                switch (buttonid) {
                    case Buttons::CLOSE:
                        fn_npc_talk_more(type_, -1);
                        break;
                    case Buttons::OK:
                        fn_npc_talk_more(type_, 1);
                        break;
                }
                break;
            case TalkType::SEND_NEXT_PREV:
                // Turning back a page is what the button that is gone used to be for; the only answer
                // a press can still give is that the reader is done with the character.
                if (buttonid == Buttons::CLOSE) {
                    fn_npc_talk_more(type_, -1);
                }
                break;
            case TalkType::SEND_PREV:
                // A page offered only a way back, and no answer was ever sent for it. Closing is the
                // whole of what is left.
                break;
            case TalkType::SEND_YESNO:
                // Type = 1
                switch (buttonid) {
                    case Buttons::CLOSE:
                        fn_npc_talk_more(type_, -1);
                        break;
                    case Buttons::NO:
                        fn_npc_talk_more(type_, 0);
                        break;
                    case Buttons::YES:
                        fn_npc_talk_more(type_, 1);
                        break;
                }
                break;
            case TalkType::SEND_ACCEPT_DECLINE:
                // Type = 1
                switch (buttonid) {
                    case Buttons::CLOSE:
                        fn_npc_talk_more(type_, -1);
                        break;
                    case Buttons::QNO:
                        fn_npc_talk_more(type_, 0);
                        break;
                    case Buttons::QYES:
                        fn_npc_talk_more(type_, 1);
                        break;
                }
                break;
            case TalkType::SEND_GET_TEXT:
                // TODO: What is this?
                break;
            case TalkType::SEND_GET_NUMBER:
                // Type = 3
                switch (buttonid) {
                    case Buttons::CLOSE:
                        fn_npc_talk_more(type_, 0);
                        break;
                    case Buttons::OK:
                        fn_npc_talk_more(type_, 1);
                        break;
                }
                break;
            case TalkType::SEND_SIMPLE:
                // Type = 4
                switch (buttonid) {
                    case Buttons::CLOSE:
                        fn_npc_talk_more(type_, 0);
                        break;
                    default:
                        // The id an answer is pressed under is its number, offset by the slot the
                        // answer options were given in the table of buttons.
                        fn_npc_talk_more(buttonid - Buttons::TEXT_BUTTON);
                        break;
                }
                break;
            default:
                break;
        }

        for (const auto &row: content_.rows_) {
            if (row.option_ < 0) {
                continue;
            }

            auto it = buttons_.find(Buttons::TEXT_BUTTON + row.option_);
            if (it != buttons_.end()) {
                buttons_.erase(it);
            }
        }

        return Button::State::NORMAL;
    }

    void UINpcTalk::send_scroll(double yoffset) {
        if (show_slider_ && slider_.isenabled()) {
            slider_.send_scroll(yoffset);
        }
    }

    Cursor::State UINpcTalk::send_cursor(bool clicked, Point<int16_t> cursorpos) {
        Point<int16_t> cursor_relative = cursorpos - position_;

        if (show_slider_ && slider_.isenabled()) {
            if (Cursor::State sstate =
                    slider_.send_cursor(cursor_relative, clicked)) {
                // A press on the bar changes which frame the thumb is wearing, and the thumb is art
                // the window is showing rather than art this pass still paints. The scroll itself
                // marks the dialog dirty through the move callback; this covers the presses that
                // only change the look.
                native_dirty_ = true;

                return sstate;
            }
        }

        Cursor::State estate = UIElement::send_cursor(clicked, cursorpos);

        if (estate == Cursor::State::CLICKING && clicked) {
            if (typing()) {
                // A tap anywhere that is not a control puts up the rest of what is being said at once.
                // The hold is paid for again from there, so a page the reader was already done with is
                // still looked at rather than walked past in the same instant.
                conversation_.revealed_ = conversation_.text_.size();
                conversation_.typed_ = 0;
                conversation_.held_ = 0;

                reveal();
                native_dirty_ = true;
            } else if (!answering_) {
                const Point<int16_t> shift = scroll_shift();

                for (const auto &row: content_.rows_) {
                    if (row.option_ < 0) {
                        continue;
                    }

                    // The box the line is drawn in, worked out the same way the drawing works it out,
                    // so an answer cannot be a place away from the words it is pressed on.
                    const Point<int16_t> lt = position_ + row.position_ + shift;
                    const Rectangle<int16_t> box(lt, lt + row.text_.dimensions());

                    if (box.contains(cursorpos)) {
                        button_pressed(Buttons::TEXT_BUTTON + row.option_);
                        break;
                    }
                }
            }
        }

        return estate;
    }

    void UINpcTalk::send_key(int32_t keycode, bool pressed, bool escape) {
        if (pressed && escape) {
            conversation_.paging_ = false;
            conversation_.awaiting_ = false;
            answering_ = false;

            deactivate();
            hide_native();

            fn_npc_talk_more(type_, 0);
        }
    }

    UIElement::Type UINpcTalk::get_type() const {
        return TYPE;
    }

    UINpcTalk::TalkType UINpcTalk::get_by_value(int8_t value,
                                                uint8_t style_b0,
                                                uint8_t style_b1) {
        if (value > TalkType::NONE && value < TalkType::LENGTH) {
            if (value == 0) {
                bool is_send_ok = (style_b0 == 0) && (style_b1 == 0);
                bool is_next_prev = (style_b0 == 1) && (style_b1 == 1);
                bool is_next = (style_b0 == 0) && (style_b1 == 1);
                bool is_prev = (style_b0 == 1) && (style_b1 == 0);

                if (is_send_ok) {
                    return TalkType::SEND_OK;
                }
                if (is_next_prev) {
                    return TalkType::SEND_NEXT_PREV;
                }
                if (is_next) {
                    return TalkType::SEND_NEXT;
                }
                if (is_prev) {
                    return TalkType::SEND_PREV;
                }
            } else if (value == 4) {
                return TalkType::SEND_SIMPLE;
            }
            return static_cast<TalkType>(value);
        }

        return TalkType::NONE;
    }

// A named reference in what the character says - `#p1090000#`, `#m120000000#`, `#t4032496#` - is an
// id standing where a name belongs. The name comes out of the same String archive the rest of the
// interface reads, so the dialog, the minimap and the inventory never disagree about what something
// is called, and a resource that is in another language carries into the dialog with it.
//
// The layout marks (`#b #k #r #c #L #l`) are deliberately left in place: those are not names, and
// the text layout eats them to pick a colour or start an option.
//
// A reference whose name the archive does not have is dropped rather than printed as the digits it
// failed to resolve.
    std::string UINpcTalk::format_text(const std::string &tx,
                                       const int32_t &npcid) {
        static constexpr std::string_view LAYOUT_MARKS = "bkrcLl";
        // The kinds the archive can answer. Anything else is left exactly as it was, because a
        // letter with no known meaning is not permission to swallow the text up to its next hash.
        static constexpr std::string_view NAMED_KINDS = "pmoivqsh";

        auto name_of = [&](char kind, const std::string &id) {
            switch (kind) {
                case 'p':
                    return std::string(img::wz::string["Npc.img"][id]["name"]);
                case 'o':
                    return std::string(img::wz::string["Mob.img"][id]["name"]);
                case 'm':
                    return NxHelper::Map::get_map_info_by_id(std::stoi(id)).name;
                case 't':
                case 'i':
                case 'v':
                    // The last two are icons the text window cannot draw; the name is what the
                    // reader needs from them.
                    return ItemData::get(std::stoi(id)).get_name();
                case 'q':
                case 's':
                    return std::string(img::wz::string["Skill.img"][id]["name"]);
                case 'h':
                    return Stage::get().get_player().get_name();
                default:
                    return std::string();
            }
        };

        std::string formatted_text;
        formatted_text.reserve(tx.size());

        for (size_t pos = 0; pos < tx.size();) {
            if (tx[pos] != '#') {
                formatted_text += tx[pos++];
                continue;
            }

            const char kind = pos + 1 < tx.size() ? tx[pos + 1] : '\0';

            if (LAYOUT_MARKS.find(kind) != std::string_view::npos) {
                formatted_text += tx[pos++];

                if (pos < tx.size()) {
                    formatted_text += tx[pos++];
                }

                continue;
            }

            const bool named = NAMED_KINDS.find(kind) != std::string_view::npos;
            const size_t last = tx.find('#', pos + 2);
            std::string id =
                last == std::string::npos ? std::string()
                                          : tx.substr(pos + 2, last - pos - 2);

            // The id is digits, with the one space of `#h #` allowed to stand in for none of them.
            if (id == " ") {
                id.clear();
            }

            const bool digits = !id.empty()
                && id.find_first_not_of("0123456789") == std::string::npos;

            if (!named || last == std::string::npos
                || !(digits || (kind == 'h' && id.empty()))) {
                formatted_text += tx[pos++];
                continue;
            }

            if (id.empty()) {
                // `#p#` from a character that is speaking about itself.
                id = std::to_string(npcid);
            }

            formatted_text += name_of(kind, id);
            pos = last + 1;
        }

        return formatted_text;
    }

    void UINpcTalk::change_text(int32_t npcid,
                                int8_t msgtype,
                                uint8_t style_b0,
                                uint8_t style_b1,
                                int8_t speakerbyte,
                                const std::string &tx) {
        type_ = get_by_value(msgtype, style_b0, style_b1);

        std::string page = format_text(tx, npcid);
        normalize_breaks(page);

        // A character says a conversation a packet at a time, and the interface's slot for the dialog
        // is replaced by every one of those packets. So the words belong to the conversation rather
        // than to this object: a page that was asked for by the one before it joins the end of what
        // has been said, and anything else - a first page, or an answer that opened another branch -
        // starts the reading over.
        if (conversation_.awaiting_ && conversation_.npcid_ == npcid) {
            conversation_.text_ += '\n';
            conversation_.text_ += page;
            conversation_.awaiting_ = false;
            conversation_.held_ = 0;

            // The reader was looking somewhere else in the words, which is theirs to do; a page the
            // character has only now started saying is a thing being said, and is followed.
            conversation_.follow_ = true;
        } else {
            conversation_.begin(npcid, page);
        }

        conversation_.lines_ = line_plan(conversation_.text_);

        // Only a page the character turns by itself is followed up. A page with a choice on it waits
        // for the reader, and a page that only ever offered a way back would have the same words run
        // together twice.
        conversation_.paging_ = type_ == TalkType::SEND_NEXT
                                || type_ == TalkType::SEND_NEXT_PREV;

        if (speakerbyte == 0) {
            std::string strid = std::to_string(npcid);
            strid.insert(0, 7 - strid.size(), '0');
            strid.append(".img");

            speaker_ = img::wz::npc[strid]["stand"]["0"];

            std::string namestr =
                    img::wz::string["Npc.img"][std::to_string(npcid)]["name"];
            name_.change_text(namestr);
        } else {
            speaker_ = Texture();
            name_.change_text("");
        }

        // The options are read out of the whole of the words, not out of the part said so far, so that
        // a choice is known to exist before the line that ends it has been finished. Whether it can be
        // pressed is settled by what is on the screen, which is the other content below.
        const NPCTalkContent all = content_for(conversation_.text_.size());

        for (const auto &row: all.rows_) {
            if (row.option_ < 0) {
                continue;
            }

            buttons_[Buttons::TEXT_BUTTON
                     + row.option_] = std::make_unique<TextButton>(row.text_, row.option_);
        }

        // The frame is one size for every dialog, so the words are measured for one thing only:
        // whether they reach past the bottom of it and so need the bar. Sized to the words instead,
        // the window stretched downward a line at a time while the character talked and took the
        // buttons with it - a shape changing under the reader's own eyes is not one they can read.
        const Text all_said(Text::Font::A18M,
                            Text::Alignment::LEFT,
                            Color::Name::DARKGREY,
                            conversation_.text_,
                            320);
        const int16_t text_height = all_said.height();

        height_ = DIALOG_HEIGHT_;
        show_slider_ = false;
        row_max_ = unit_rows_;

        // How far the bar has to carry the words for the last line of them to be reachable. They
        // start this far below the head of the frame and are read through a band height_ tall, so the
        // overhang is what is left of them once that band is filled; a row of the bar is worth
        // SCROLL_ROW_ of it.
        const int32_t overhang = 19 + text_height - (top_.height() - 1 + height_);

        if (overhang > 0) {
            show_slider_ = true;

            row_max_ = static_cast<int16_t>(std::ceil(
                overhang / static_cast<double>(SCROLL_ROW_))) + unit_rows_;

            conversation_.scroll_ = std::min(conversation_.scroll_,
                                             static_cast<int16_t>(row_max_ - unit_rows_));

            const int16_t slider_y = top_.height() - 7;

            slider_ = Slider(Slider::Type::DEFAULT_SILVER,
                            Range<int16_t>(slider_y, slider_y + height_),
                            top_.width() - 26,
                            unit_rows_,
                            row_max_,
                            onmoved_);

            // A conversation that is already being looked at keeps its place; the new page is
            // below, and the bar has to say so.
            slider_.setrows(conversation_.scroll_, unit_rows_, row_max_);
        } else {
            conversation_.scroll_ = 0;
        }

        // The foot of the frame is the same place for every dialog, which is the other half of what
        // makes the buttons stay where the reader learned them.
        for (auto &button: buttons_) {
            button.second->set_active(false);
            button.second->set_state(Button::State::NORMAL);
        }

        const int16_t y_cord = height_ + 48;

        buttons_[Buttons::CLOSE]->set_position(Point<int16_t>(9, y_cord));
        buttons_[Buttons::CLOSE]->set_active(true);

        switch (type_) {
            case TalkType::SEND_OK:
                buttons_[Buttons::OK]->set_position(Point<int16_t>(469, y_cord));
                buttons_[Buttons::OK]->set_active(true);
                break;
            case TalkType::SEND_YESNO: {
                const Point<int16_t> yes_position = Point<int16_t>(389, y_cord);

                buttons_[Buttons::YES]->set_position(yes_position);
                buttons_[Buttons::NO]->set_position(yes_position
                                                    + Point<int16_t>(65, 0));
                buttons_[Buttons::YES]->set_active(true);
                buttons_[Buttons::NO]->set_active(true);
                break;
            }
            case TalkType::SEND_ACCEPT_DECLINE:
                buttons_[Buttons::QYES]->set_position(Point<int16_t>(386, y_cord));
                buttons_[Buttons::QNO]->set_position(Point<int16_t>(451, y_cord));
                buttons_[Buttons::QYES]->set_active(true);
                buttons_[Buttons::QNO]->set_active(true);
                break;
            case TalkType::SEND_TEXT_ONLY:
            case TalkType::SEND_SIMPLE:
            case TalkType::SEND_GET_TEXT:
            case TalkType::SEND_GET_NUMBER:
            case TalkType::SEND_NEXT:
            case TalkType::SEND_NEXT_PREV:
            case TalkType::SEND_PREV:
            case TalkType::NONE:
            default:
                break;
        }

        // The frame is put where the first page of this conversation put it, so that the words already
        // on the screen stay put when a page is added underneath them: the dialog grows downward from
        // there rather than re-centering itself on the new words.
        if (!conversation_.anchored_) {
            // The dialog is put in the middle of the screen the interface is laid out on - which is
            // magnified onto the view - and a little above that centre, as it was when the screen it
            // was measured against was 800x600.
            const int16_t ui_width =
                    Constants::Constants::get().get_ui_viewwidth();
            const int16_t ui_height =
                    Constants::Constants::get().get_ui_viewheight();

            conversation_.frame_ = Point<int16_t>(
                    ui_width / 2 - top_.width() / 2,
                    ui_height * 2 / 5 - height_ / 2);
            conversation_.anchored_ = true;
        }

        position_ = conversation_.frame_;
        dimension_ = Point<int16_t>(top_.width(), height_ + 120);

        // Everything the window will show for this page: the part of the words the reader has been
        // shown so far, and where the view is looking at them from.
        reveal();

        // A page of what is being said is a different dialog as far as the screen is concerned, even
        // though the frame is the same one.
        native_dirty_ = true;
    }

    std::vector<UINpcTalk::NPCTalkContent::Line> UINpcTalk::line_plan(const std::string &text) {
        using Line = NPCTalkContent::Line;
        static const std::regex option_link("#L(\\d{1,2})#");

        std::vector<Line> lines;

        // An answer is read in blue, and what makes a line an answer is the link that opens it - so
        // the reading is done over the whole of a line. Done over the part of it said so far, a line
        // would be an ordinary one until its link was finished and an answer from then on.
        //
        // The link is `#L<n>#` on its own; what follows it is the words of the answer. The mark that
        // used to be asked for after it is only there when the script puts a colour mark first, so
        // insisting on it left every plain `#L0#奥力奥力#l` an ordinary line - readable, but a thing
        // the reader could never press.
        Color::Name colour = Color::Name::DARKGREY;
        size_t start = 0;

        while (start < text.size()) {
            size_t stop = text.find('\n', start);

            if (stop == std::string::npos) {
                stop = text.size();
            }

            const std::string line = text.substr(start, stop - start);
            std::smatch match;
            const bool answer = std::regex_search(line, match, option_link);

            int16_t number = -1;
            Color::Name ink = Color::Name::DARKGREY;

            if (answer) {
                if (line.find("#b") != std::string::npos) {
                    colour = Color::Name::BLUE;
                }

                ink = colour;
                number = static_cast<int16_t>(std::stoi(match[1].str()));
            } else if (line.size() >= 2 && line.rfind("#b") == line.size() - 2) {
                // A line that closes on the opening mark is the one that puts the answers after it in
                // blue.
                colour = Color::Name::BLUE;
            }

            lines.push_back({start, stop, number,
                             Text(Text::Font::A18M, Text::Alignment::LEFT, ink, line, 320)});

            start = stop + 1;
        }

        return lines;
    }

    UINpcTalk::NPCTalkContent UINpcTalk::content_for(size_t shown) const {
        NPCTalkContent content;

        for (const auto &line: conversation_.lines_) {
            if (line.start_ >= shown) {
                break;
            }

            const size_t end = std::min(line.end_, shown);

            // The showing is kept in bytes of the conversation; the layout is lettered in code
            // points. A byte that opens a sequence is one letter, the bytes that continue it are not.
            size_t codepoints = 0;

            for (size_t i = line.start_; i < end; i++) {
                if ((static_cast<unsigned char>(conversation_.text_[i]) & 0xC0) != 0x80) {
                    codepoints++;
                }
            }

            content.append(line, codepoints);
        }

        return content;
    }

    void UINpcTalk::NPCTalkContent::append(const Line &line, size_t shown) {
        rows_.push_back({line.whole_.revealed(shown), line.option_, Point<int16_t>()});
    }

    void UINpcTalk::reset_content_pos() {
        // Where each line stands inside the frame, in the order it is read. The view's place in the
        // words is not kept here: it is applied where the lines are put up and where they are pressed
        // on, from the same arithmetic, so that the two can never be a screenfull apart.
        const int16_t x = show_slider_ ? 162 : 166;

        int32_t height = 19;

        for (auto &row: content_.rows_) {
            row.position_ = Point<int16_t>(x, static_cast<int16_t>(height));

            height += row.text_.height();
        }
    }
}  // namespace ms