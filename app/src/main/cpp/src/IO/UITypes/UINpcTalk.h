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
#include <map>
#include <string>

#include "../Components/Slider.h"
#include "../Graphics/Text.h"
#include "../NativeNpcTalk.h"
#include "../UIElement.h"
#include "TextButton.h"

namespace ms {
class UINpcTalk : public UIElement {
public:
    enum TalkType : int8_t {
        NONE = -1,
        SEND_TEXT_ONLY,
        SEND_YESNO,
        SEND_SIMPLE = 4,
        SEND_ACCEPT_DECLINE = 12,
        // below are determined by 2 style bytes when msgtype = 0
        SEND_OK,
        SEND_NEXT,
        SEND_NEXT_PREV,
        SEND_PREV,
        SEND_GET_TEXT,
        SEND_GET_NUMBER,
        LENGTH
    };

    struct NPCTalkContent {
        // One line of what is being said, read out of the whole of it before any of it is put on the
        // screen: where it stands in the conversation's words, whether it offers an answer, and in
        // what colour.
        struct Line {
            size_t start_ = 0;
            size_t end_ = 0;
            int16_t option_ = -1;
            // The line laid out whole, once. What the reader is shown of it is a prefix of this
            // layout, so the column's breaks are the ones the finished line will have and no word
            // already on the screen moves when the next one arrives.
            Text whole_;
        };

        // One line as it stands on the screen - cut short at whatever the character has said of it so
        // far, but keeping the class and the colour the whole of it was read in. The words said and
        // the answers offered used to be kept in two lists drawn one after the other, and each was
        // settled again from the part said so far, so a line changed from one list to the other in
        // front of the reader and jumped from above the answers to below them.
        struct Row {
            Text text_;
            int16_t option_ = -1;
            // Where it stands inside the frame, before the view is taken down the words.
            Point<int16_t> position_;
        };

        void append(const Line &line, size_t shown);

        std::vector<Row> rows_;
    };

    static constexpr Type TYPE = UIElement::Type::NPC_TALK;
    static constexpr bool FOCUSED = true;
    static constexpr bool TOGGLED = false;

    UINpcTalk();

    void draw(float inter) const override;

    void update() override;

    Cursor::State send_cursor(bool clicked, Point<int16_t> cursorpos) override;

    void send_scroll(double yoffset) override;

    void send_key(int32_t keycode, bool pressed, bool escape) override;

    UIElement::Type get_type() const override;

    void change_text(int32_t npcid,
                     int8_t msgtype,
                     uint8_t style_b0,
                     uint8_t style_b1,
                     int8_t speaker,
                     const std::string &text);

    // Takes the dialog down because something else has taken the screen - a shop opened, the map
    // changed - so a dialog left waiting on an answer does not stand over what replaced it.
    static void dismiss();

protected:
    Button::State button_pressed(uint16_t buttonid) override;

private:
    TalkType get_by_value(int8_t value, uint8_t style_b0, uint8_t style_b1);

    std::string format_text(const std::string &tx, const int32_t &npcid);

    // Reads the whole of what is being said and settles once, for each of its lines, whether it is
    // spoken or answered and in what colour it is read. Reading it off the part said so far instead is
    // what let a line change its mind in front of the reader.
    std::vector<NPCTalkContent::Line> line_plan(const std::string &text);

    // The dialog as it stands once the reader has been shown this many bytes of the words: every line
    // begun is a line of its own, cut short where the showing stopped, in the order they are read.
    NPCTalkContent content_for(size_t shown) const;

    // Where the words are put relative to the frame, before the view is taken down them.
    Point<int16_t> scroll_shift() const;

    void reset_content_pos();

    // The same dialog as a record of a native window's display list, in the order this pass would
    // have painted it. Returns false for a dialog that cannot be handed over whole - a button whose
    // plain art is an animation, a frame whose chrome did not load - and the caller then keeps
    // painting it the way it always did.
    bool build(NativeNpcTalk::List &list) const;

    // Takes the words off the native window. Called wherever the dialog stops being drawn, because
    // a panel is not skipped along with the pass: nothing reaches it once the game has let go of the
    // dialog, so leaving it standing would keep a finished conversation on the screen.
    void hide_native() const;

    // Puts up as much of what is being said as the reader has been shown, and lets the view ride
    // along after it.
    void reveal();

    // Moves the view down far enough that the character being said is still inside the frame.
    void follow_caret();

    // Whether there is more coming than the reader has been shown yet.
    bool typing() const;

    // The frame is this tall for every dialog, however much or little there is to read. It used to be
    // sized to the words, which once the pages run together into one conversation meant the window
    // stretched downward a line at a time while the character talked and took the buttons with it - a
    // shape changing under the reader's own eyes is not one they can read. This is the tallest the
    // frame used to be able to become, so nothing that fitted before has to be scrolled now; anything
    // taller goes to the bar.
    static constexpr int16_t DIALOG_HEIGHT_ = 248;

    // How far one row of the scroll bar carries the words, in the units the interface is laid out in.
    // The bar used to move a whole windowful at a time, because that is what a page was; read as one
    // running text, a windowful is far too big a step to be looking for a line in. One line of the
    // dialog's own lettering is what a row is worth: the words are set in A18M, whose line is the
    // height of an 18-pixel face spaced at 1.35, and every height the layout gives back is a multiple
    // of it.
    static constexpr int16_t SCROLL_ROW_ = 25;

    // What the character is saying, and how far the reader has been shown of it.
    //
    // This belongs to the conversation rather than to the window, because the interface's slot for the
    // dialog is replaced by the packet for every page: the object drawing the second page of what a
    // character said is not the one that drew the first. The pages are added to the end of the same
    // words instead of taking the place of the page before, which is what lets a conversation be read
    // by scrolling.
    struct Conversation {
        void begin(int32_t npcid, std::string said);

        int32_t npcid_ = 0;
        std::string text_;

        // Every line of text_, read out of the whole of it before any of it was said: what each line is
        // and where it stands. Kept beside the words because the words are let out a piece at a time,
        // and a line has to stay the line it was known as while the character is saying it.
        std::vector<NPCTalkContent::Line> lines_;

        // Bytes of text_ the reader has been shown. It only ever stops in front of a character that
        // draws, so a mark between the words is never seen half of.
        size_t revealed_ = 0;

        // Milliseconds kept: how long the next character has been owed, and how long this page has
        // been finished being said for.
        uint16_t typed_ = 0;
        uint16_t held_ = 0;

        // The page being read is one of several, so the next is asked for by itself once this one has
        // been said, instead of waiting for a press of a button that is no longer there. Awaiting says
        // the asking has happened, which is also what tells a page that was asked for from a
        // conversation that is starting over.
        bool paging_ = false;
        bool awaiting_ = false;

        // Where the frame was put for the first page, so that the words already on the screen do not
        // move when a page is added underneath them.
        bool anchored_ = false;
        Point<int16_t> frame_;

        // Rows the reader is looking at, and whether the view still rides after the words. Taking the
        // bar in hand is theirs until the character starts saying something new.
        int16_t scroll_ = 0;
        bool follow_ = true;
    };

    enum Buttons {
        ALL_LEVEL,
        CLOSE,
        MY_LEVEL,
        NO,
        OK,
        QAFTER,
        QCNO,
        QCYES,
        QGIVEUP,
        QNO,
        QSTART,
        QYES,
        YES,
        TEXT_BUTTON
    };

    Texture top_;
    Texture fill_;
    Texture bottom_;
    Texture nametag_;
    Texture speaker_;

    // Where each of the dialog's buttons was read from. A native window is handed its chrome by name
    // rather than by pixels, so the path has to be kept beside the button that was built from it -
    // recorded at the same moment, or the two would come apart the first time one of them moved.
    std::map<uint16_t, std::string> button_assets_;

    // What was last handed to the native window, and whether it is what is on the screen. The dialog
    // is rebuilt from scratch for every page a character turns to, so these belong to this object
    // rather than to the window.
    mutable bool native_shown_;
    mutable bool native_dirty_;

    // An answer has been sent and the next page has not come back yet. The dialog is kept up while the
    // answer is in flight so a conversation that continues is not blanked between the press and the
    // reply; if nothing comes the wait runs out and the dialog is taken down.
    bool answering_ = false;
    uint16_t answer_ms_ = 0;

    NPCTalkContent content_;
    Text name_;

    int16_t height_;
    int16_t unit_rows_;
    int16_t row_max_;

    bool show_slider_;
    Slider slider_;
    TalkType type_;

    // What is being said, kept between the packets that carry it a page at a time.
    static Conversation conversation_;

    std::function<void(bool)> onmoved_;
};
}  // namespace ms