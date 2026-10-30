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
#include "UIExplorerCreation.h"

#include <algorithm>

#include <imgwz/wz.hpp>

#include "../../Audio/Audio.h"
#include "../../Configuration.h"
#include "../../Data/ItemData.h"
#include "../../Graphics/PanelRenderer.h"
#include "../../Net/Packets/CharCreationPackets.h"
#include "../Components/MapleButton.h"
#include "../UI.h"
#include "UICharSelect.h"
#include "UILoginNotice.h"

namespace ms {
namespace {
auto fn_create_char = []<typename... T>(T && ...args) {
    CreateCharPacket(std::forward<T>(args)...).dispatch();
};

// Where the board sits in the interface's own coordinates: its box is x 486..711, y 95..472, so this
// is the point it is grown about. Growing about the board's own centre keeps it over the part of the
// picture it has always covered instead of drifting into a corner of the screen.
constexpr Point<int16_t> BOARD_CENTRE(598, 283);

// Growing about the centre alone lifts the board by 67, and the frame's clear window - which is
// x 37..761, y 39..548 of the interface - only lets a board that starts at y 95 be lifted by 56
// before its top edge crosses the border. The frame stays on the interface, which the panel now
// composites over, so a board that crossed it would cover the border instead of hiding under it;
// either way it has to stay inside.
constexpr int16_t BOARD_LIFT = 59;

// The translation from the interface's coordinates into the panel's for everything on the board.
Point<int16_t> board_shift() {
    Point<int16_t> centred =
        Constants::to_panel_space(BOARD_CENTRE) - BOARD_CENTRE;

    return Point<int16_t>(centred.x(), -BOARD_LIFT);
}
}  // namespace

auto fn_name_char = [](auto name) { NameCharPacket(name).dispatch(); };

UIExplorerCreation::UIExplorerCreation() :
    UIElement(board_shift(), Point<int16_t>(800, 600)) {
    gender_ = false;
    charset_ = false;
    named_ = false;

    std::string version_text = Configuration::get().get_version();
    version_ = Text(Text::Font::A11M,
                    Text::Alignment::LEFT,
                    Color::Name::LEMONGRASS,
                    "Ver. " + version_text);

    img::node Login = img::wz::ui["Login.img"];
    img::node Common = Login["Common"];
    img::node CustomizeChar = Login["CustomizeChar"]["000"];
    img::node back = img::wz::map001["Back"]["login.img"]["back"];
    img::node signboard =
        img::wz::mapLatest["Obj"]["login.img"]["NewChar"]["signboard"];
    img::node board = CustomizeChar["board"];
    img::node genderSelect = CustomizeChar["genderSelect"];

    sky_ = back["2"];
    cloud_ = back["27"];

    scenery_ = Sprite(back["14"], Point<int16_t>(250, 302));

    sprites_pinned_.emplace_back(signboard["2"],
                                 DrawArgument(Point<int16_t>(234, 235), 2.0f));
    sprites_gender_select_.emplace_back(board["genderTop"],
                                        Point<int16_t>(486, 95));
    sprites_gender_select_.emplace_back(board["boardMid"],
                                        Point<int16_t>(486, 209));
    sprites_gender_select_.emplace_back(board["boardBottom"],
                                        Point<int16_t>(486, 329));
    sprites_lookboard_.emplace_back(CustomizeChar["charSet"],
                                    Point<int16_t>(486, 95));

    for (size_t i = 0; i <= 5; i++) {
        size_t f = i;

        if (i >= 2) {
            f++;
        }

        sprites_lookboard_.emplace_back(CustomizeChar["avatarSel"][i]["normal"],
                                        Point<int16_t>(497, 197 + (f * 18)));
    }

    buttons_[Buttons::BT_CHARC_GENDER_M] =
        std::make_unique<MapleButton>(genderSelect["male"],
                                      Point<int16_t>(487, 109));
    buttons_[Buttons::BT_CHARC_GEMDER_F] =
        std::make_unique<MapleButton>(genderSelect["female"],
                                      Point<int16_t>(485, 109));
    buttons_[Buttons::BT_CHARC_FACEL] =
        std::make_unique<MapleButton>(CustomizeChar["BtLeft"],
                                      Point<int16_t>(552, 198 + (0 * 18)));
    buttons_[Buttons::BT_CHARC_FACER] =
        std::make_unique<MapleButton>(CustomizeChar["BtRight"],
                                      Point<int16_t>(684, 198 + (0 * 18)));
    buttons_[Buttons::BT_CHARC_HAIRL] =
        std::make_unique<MapleButton>(CustomizeChar["BtLeft"],
                                      Point<int16_t>(552, 198 + (1 * 18)));
    buttons_[Buttons::BT_CHARC_HAIRR] =
        std::make_unique<MapleButton>(CustomizeChar["BtRight"],
                                      Point<int16_t>(684, 198 + (1 * 18)));
    buttons_[Buttons::BT_CHARC_SKINL] =
        std::make_unique<MapleButton>(CustomizeChar["BtLeft"],
                                      Point<int16_t>(552, 198 + (3 * 18)));
    buttons_[Buttons::BT_CHARC_SKINR] =
        std::make_unique<MapleButton>(CustomizeChar["BtRight"],
                                      Point<int16_t>(684, 198 + (3 * 18)));
    buttons_[Buttons::BT_CHARC_TOPL] =
        std::make_unique<MapleButton>(CustomizeChar["BtLeft"],
                                      Point<int16_t>(552, 198 + (4 * 18)));
    buttons_[Buttons::BT_CHARC_TOPR] =
        std::make_unique<MapleButton>(CustomizeChar["BtRight"],
                                      Point<int16_t>(684, 198 + (4 * 18)));
    buttons_[Buttons::BT_CHARC_SHOESL] =
        std::make_unique<MapleButton>(CustomizeChar["BtLeft"],
                                      Point<int16_t>(552, 198 + (5 * 18)));
    buttons_[Buttons::BT_CHARC_SHOESR] =
        std::make_unique<MapleButton>(CustomizeChar["BtRight"],
                                      Point<int16_t>(684, 198 + (5 * 18)));
    buttons_[Buttons::BT_CHARC_WEPL] =
        std::make_unique<MapleButton>(CustomizeChar["BtLeft"],
                                      Point<int16_t>(552, 198 + (6 * 18)));
    buttons_[Buttons::BT_CHARC_WEPR] =
        std::make_unique<MapleButton>(CustomizeChar["BtRight"],
                                      Point<int16_t>(684, 198 + (6 * 18)));

    for (size_t i = 0; i <= 7; i++) {
        buttons_[Buttons::BT_CHARC_HAIRC0 + i] =
            std::make_unique<MapleButton>(CustomizeChar["hairSelect"][i],
                                          Point<int16_t>(549 + (i * 15), 234));
        buttons_[Buttons::BT_CHARC_HAIRC0 + i]->set_active(false);
    }

    buttons_[Buttons::BT_CHARC_FACEL]->set_active(false);
    buttons_[Buttons::BT_CHARC_FACER]->set_active(false);
    buttons_[Buttons::BT_CHARC_HAIRL]->set_active(false);
    buttons_[Buttons::BT_CHARC_HAIRR]->set_active(false);
    buttons_[Buttons::BT_CHARC_SKINL]->set_active(false);
    buttons_[Buttons::BT_CHARC_SKINR]->set_active(false);
    buttons_[Buttons::BT_CHARC_TOPL]->set_active(false);
    buttons_[Buttons::BT_CHARC_TOPR]->set_active(false);
    buttons_[Buttons::BT_CHARC_SHOESL]->set_active(false);
    buttons_[Buttons::BT_CHARC_SHOESR]->set_active(false);
    buttons_[Buttons::BT_CHARC_WEPL]->set_active(false);
    buttons_[Buttons::BT_CHARC_WEPR]->set_active(false);

    buttons_[Buttons::BT_CHARC_OK] =
        std::make_unique<MapleButton>(CustomizeChar["BtYes"],
                                      Point<int16_t>(514, 394));
    buttons_[Buttons::BT_CHARC_CANCEL] =
        std::make_unique<MapleButton>(CustomizeChar["BtNo"],
                                      Point<int16_t>(590, 394));

    nameboard_ = CustomizeChar["charName"];
    namechar_ = Textfield(
        Text::Font::A13M,
        Text::Alignment::LEFT,
        Color::Name::WHITE,
        Rectangle<int16_t>(Point<int16_t>(522, 195), Point<int16_t>(630, 253)),
        12);

    sprites_pinned_.emplace_back(Common["frame"], Point<int16_t>(400, 300));
    sprites_pinned_.emplace_back(Common["step"]["3"], Point<int16_t>(40, 0));

    // The arrow is pinned to the corner of the screen, not to the board, so it stays on the
    // interface at the size every other login screen draws it and is not in the panel's button list.
    back_button_ = std::make_unique<MapleButton>(Login["Common"]["BtStart"],
                                                 Point<int16_t>(0, 515));

    namechar_.set_state(Textfield::DISABLED);

    namechar_.set_enter_callback(
        [&](const std::string &) { button_pressed(Buttons::BT_CHARC_OK); });

    namechar_.set_key_callback(KeyAction::Id::ESCAPE, [&]() {
        button_pressed(Buttons::BT_CHARC_CANCEL);
    });

    facename_ =
        Text(Text::Font::A11M, Text::Alignment::CENTER, Color::Name::BLACK);
    hairname_ =
        Text(Text::Font::A11M, Text::Alignment::CENTER, Color::Name::BLACK);
    bodyname_ =
        Text(Text::Font::A11M, Text::Alignment::CENTER, Color::Name::BLACK);
    topname_ =
        Text(Text::Font::A11M, Text::Alignment::CENTER, Color::Name::BLACK);
    shoename_ =
        Text(Text::Font::A11M, Text::Alignment::CENTER, Color::Name::BLACK);
    wepname_ =
        Text(Text::Font::A11M, Text::Alignment::CENTER, Color::Name::BLACK);

    // Info is the Adventurer set, Premium was the Noblesse set and Orient the
    // Legend set; every look they offered is selectable here.
    img::node mkinfo = img::wz::etc["MakeCharInfo.img"];

    for (size_t i = 0; i < 2; i++) {
        bool f = (i == 0);
        std::string gender = f ? "Female" : "Male";

        std::vector<img::node> charsets = {
            mkinfo["Info"]["Char" + gender],
            mkinfo["PremiumChar" + gender],
            mkinfo["OrientChar" + gender]};

        auto add_unique = [](auto &ids, auto value) {
            if (std::find(ids.begin(), ids.end(), value) == ids.end()) {
                ids.push_back(value);
            }
        };

        for (const auto &CharGender : charsets) {
            for (const auto &node : CharGender) {
                int num = stoi(node.name());

                for (const auto &idnode : node) {
                    int32_t value = idnode;

                    switch (num) {
                        case 0: add_unique(faces_[f], value); break;
                        case 1: add_unique(hairs_[f], value); break;
                        case 2:
                            add_unique(haircolors_[f],
                                       static_cast<uint8_t>(value));
                            break;
                        case 3:
                            add_unique(skins_[f], static_cast<uint8_t>(value));
                            break;
                        case 4: add_unique(tops_[f], value); break;
                        case 5: add_unique(bots_[f], value); break;
                        case 6: add_unique(shoes_[f], value); break;
                        case 7: add_unique(weapons_[f], value); break;
                    }
                }
            }
        }
    }

    female_ = false;
    randomize_look();

    newchar_.set_direction(true);

    cloudfx_ = 200.0f;
}

void UIExplorerCreation::draw(float inter) const {
    // The backdrop belongs to the interface; only the board goes onto the panel, which now
    // composites over the interface, so nothing here has to be shrunk on its way into that pass.
    for (size_t i = 0; i < 2; i++) {
        for (size_t k = 0; k < 800; k += sky_.width()) {
            sky_.draw(Point<int16_t>(k, (400 * i) - 100));
        }
    }

    int16_t cloudx = static_cast<int16_t>(cloudfx_) % 800;
    cloud_.draw(Point<int16_t>(cloudx - cloud_.width(), 310));
    cloud_.draw(Point<int16_t>(cloudx, 310));
    cloud_.draw(Point<int16_t>(cloudx + cloud_.width(), 310));

    scenery_.draw(Point<int16_t>(0, 0), inter);

    PanelRenderer::get().begin();

    if (!gender_) {
        for (size_t i = 0; i < sprites_gender_select_.size(); i++) {
            if (i == 1) {
                for (size_t f = 0; f <= 4; f++) {
                    sprites_gender_select_[i].draw(
                        position_ + Point<int16_t>(0, 24 * f),
                        inter);
                }
            } else {
                sprites_gender_select_[i].draw(position_, inter);
            }
        }

        UIElement::draw(inter);

        newchar_.draw(Constants::to_panel_space(Point<int16_t>(394, 339)),
                      inter);
    } else {
        if (!charset_) {
            for (const auto &sprite : sprites_lookboard_) {
                sprite.draw(position_, inter);
            }

            facename_.draw(position_ + Point<int16_t>(625, 193 + (0 * 18)));
            hairname_.draw(position_ + Point<int16_t>(625, 193 + (1 * 18)));
            bodyname_.draw(position_ + Point<int16_t>(625, 193 + (3 * 18)));
            topname_.draw(position_ + Point<int16_t>(625, 193 + (4 * 18)));
            shoename_.draw(position_ + Point<int16_t>(625, 193 + (5 * 18)));
            wepname_.draw(position_ + Point<int16_t>(625, 193 + (6 * 18)));

            newchar_.draw(Constants::to_panel_space(Point<int16_t>(394, 339)),
                          inter);

            UIElement::draw_buttons(inter);
        } else {
            if (!named_) {
                nameboard_.draw(position_ + Point<int16_t>(486, 95));

                namechar_.draw(position_);
                newchar_.draw(
                    Constants::to_panel_space(Point<int16_t>(394, 339)),
                    inter);

                UIElement::draw_buttons(inter);
            } else {
                nameboard_.draw(position_ + Point<int16_t>(486, 95));

                UIElement::draw_buttons(inter);

                for (const auto &sprite : sprites_keytype_) {
                    sprite.draw(position_, inter);
                }
            }
        }
    }

    PanelRenderer::get().end();

    // The sign, the frame, the step marker and the back arrow belong to the screen rather than to the
    // board. They stay on the interface, which the panel now composites over: the board is sized to
    // stay inside the frame's clear window, so the only thing this costs is the sign's lower bar,
    // which the top of the enlarged board runs over.
    for (const Sprite &sprite : sprites_pinned_) {
        sprite.draw(Point<int16_t>(0, 0), inter);
    }

    back_button_->draw(Point<int16_t>(0, 0));

    version_.draw(Point<int16_t>(707, 1));
}

void UIExplorerCreation::update() {
    if (!gender_) {
        for (auto &sprite : sprites_gender_select_) {
            sprite.update();
        }

        newchar_.update(Constants::TIMESTEP);
    } else {
        if (!charset_) {
            for (auto &sprite : sprites_lookboard_) {
                sprite.update();
            }

            newchar_.update(Constants::TIMESTEP);
        } else {
            if (!named_) {
                namechar_.update(position_);
                newchar_.update(Constants::TIMESTEP);
            } else {
                for (auto &sprite : sprites_keytype_) {
                    sprite.update();
                }

                namechar_.set_state(Textfield::State::DISABLED);
            }
        }
    }

    UIElement::update();

    // The backdrop and the furniture are no longer in the base class's list, because they are drawn
    // on the interface while everything that used to be next to them goes onto the panel.
    scenery_.update();

    for (auto &sprite : sprites_pinned_) {
        sprite.update();
    }

    back_button_->update();

    cloudfx_ += 0.25f;
}

Cursor::State UIExplorerCreation::send_cursor(bool clicked,
                                              Point<int16_t> cursorpos) {
    // The back arrow is on the interface rather than on the panel, so it is asked what it covers in
    // the units the pointer arrives in, before those units are converted for everything on the board.
    // The padding matches UIElement::button_at, since this button is outside the loop that applies it.
    constexpr int16_t TOUCH_PAD = 6;
    Rectangle<int16_t> backbounds =
        back_button_->bounds(Point<int16_t>(0, 0));
    backbounds =
        Rectangle<int16_t>(backbounds.get_left_top() - TOUCH_PAD,
                           backbounds.get_right_bottom() + TOUCH_PAD);

    if (backbounds.contains(cursorpos)) {
        if (back_button_->get_state() == Button::State::NORMAL) {
            Sound(Sound::Name::BUTTON_OVER).play();
            back_button_->set_state(Button::State::MOUSEOVER);
        } else if (clicked) {
            Sound(Sound::Name::BUTTON_CLICK).play();
            back_button_->set_state(button_pressed(Buttons::BT_BACK));

            return Cursor::State::IDLE;
        }

        return Cursor::State::CAN_CLICK;
    }

    if (back_button_->get_state() == Button::State::MOUSEOVER) {
        back_button_->set_state(Button::State::NORMAL);
    }

    // The pointer arrives in the interface's units and everything on the board is laid out on the
    // panel, so the tap is brought into that space before anything else is asked what it covers.
    Point<int16_t> panelpos = Constants::to_panel_space(cursorpos);

    if (namechar_.get_state() == Textfield::State::NORMAL) {
        if (namechar_.get_bounds().contains(panelpos)) {
            if (clicked) {
                namechar_.set_state(Textfield::State::FOCUSED);

                return Cursor::State::CLICKING;
            }

            return Cursor::State::IDLE;
        }
    }

    return UIElement::send_cursor(clicked, panelpos);
}

void UIExplorerCreation::send_key(int32_t keycode, bool pressed, bool escape) {
    if (pressed) {
        if (escape) {
            button_pressed(Buttons::BT_CHARC_CANCEL);
        } else if (keycode == KeyAction::Id::RETURN) {
            button_pressed(Buttons::BT_CHARC_OK);
        }
    }
}

UIElement::Type UIExplorerCreation::get_type() const {
    return TYPE;
}

void UIExplorerCreation::send_naming_result(bool nameused) {
    if (!named_) {
        if (!nameused) {
            named_ = true;

            std::string cname = namechar_.get_text();
            int32_t cface = faces_[female_][face_];
            int32_t chair = hairs_[female_][hair_];
            uint8_t chairc = haircolors_[female_][haircolor_];
            uint8_t cskin = skins_[female_][skin_];
            int32_t ctop = tops_[female_][top_];
            int32_t cbot = bots_[female_][bot_];
            int32_t cshoe = shoes_[female_][shoe_];
            int32_t cwep = weapons_[female_][weapon_];

            fn_create_char(cname,
                           1,
                           cface,
                           chair,
                           chairc,
                           cskin,
                           ctop,
                           cbot,
                           cshoe,
                           cwep,
                           female_);

            Sound(Sound::Name::SCROLL_UP).play();

            UI::get().remove(UIElement::Type::CLASS_CREATION);

            // The new entry is not known yet, so the list itself is only
            // refreshed once AddNewCharEntryHandler has taken it in.
            if (auto charselect = UI::get().get_element<UICharSelect>()) {
                charselect->get().makeactive();
            }
        } else {
            auto onok = [&]() {
                namechar_.set_state(Textfield::State::FOCUSED);

                buttons_[Buttons::BT_CHARC_OK]->set_state(
                    Button::State::NORMAL);
                buttons_[Buttons::BT_CHARC_CANCEL]->set_state(
                    Button::State::NORMAL);
            };

            UI::get().emplace<UILoginNotice>(
                UILoginNotice::Message::NAME_IN_USE,
                onok);
        }
    }
}

Button::State UIExplorerCreation::button_pressed(uint16_t buttonid) {
    switch (buttonid) {
        case Buttons::BT_CHARC_OK:
            if (!gender_) {
                gender_ = true;

                buttons_[Buttons::BT_CHARC_GENDER_M]->set_active(false);
                buttons_[Buttons::BT_CHARC_GEMDER_F]->set_active(false);

                buttons_[Buttons::BT_CHARC_SKINL]->set_active(true);
                buttons_[Buttons::BT_CHARC_SKINR]->set_active(true);

                buttons_[Buttons::BT_CHARC_FACEL]->set_active(true);
                buttons_[Buttons::BT_CHARC_FACER]->set_active(true);
                buttons_[Buttons::BT_CHARC_HAIRL]->set_active(true);
                buttons_[Buttons::BT_CHARC_HAIRR]->set_active(true);
                buttons_[Buttons::BT_CHARC_TOPL]->set_active(true);
                buttons_[Buttons::BT_CHARC_TOPR]->set_active(true);
                buttons_[Buttons::BT_CHARC_SHOESL]->set_active(true);
                buttons_[Buttons::BT_CHARC_SHOESR]->set_active(true);
                buttons_[Buttons::BT_CHARC_WEPL]->set_active(true);
                buttons_[Buttons::BT_CHARC_WEPR]->set_active(true);

                for (size_t i = 0; i <= 7; i++) {
                    buttons_[Buttons::BT_CHARC_HAIRC0 + i]->set_active(true);
                }

                buttons_[Buttons::BT_CHARC_OK]->set_position(
                    Point<int16_t>(523, 425));
                buttons_[Buttons::BT_CHARC_CANCEL]->set_position(
                    Point<int16_t>(597, 425));

                return Button::State::NORMAL;
            } else {
                if (!charset_) {
                    charset_ = true;

                    buttons_[Buttons::BT_CHARC_SKINL]->set_active(false);
                    buttons_[Buttons::BT_CHARC_SKINR]->set_active(false);

                    buttons_[Buttons::BT_CHARC_FACEL]->set_active(false);
                    buttons_[Buttons::BT_CHARC_FACER]->set_active(false);
                    buttons_[Buttons::BT_CHARC_HAIRL]->set_active(false);
                    buttons_[Buttons::BT_CHARC_HAIRR]->set_active(false);
                    buttons_[Buttons::BT_CHARC_TOPL]->set_active(false);
                    buttons_[Buttons::BT_CHARC_TOPR]->set_active(false);
                    buttons_[Buttons::BT_CHARC_SHOESL]->set_active(false);
                    buttons_[Buttons::BT_CHARC_SHOESR]->set_active(false);
                    buttons_[Buttons::BT_CHARC_WEPL]->set_active(false);
                    buttons_[Buttons::BT_CHARC_WEPR]->set_active(false);

                    for (size_t i = 0; i <= 7; i++) {
                        buttons_[Buttons::BT_CHARC_HAIRC0 + i]->set_active(
                            false);
                    }

                    buttons_[Buttons::BT_CHARC_OK]->set_position(
                        Point<int16_t>(513, 273));
                    buttons_[Buttons::BT_CHARC_CANCEL]->set_position(
                        Point<int16_t>(587, 273));

                    namechar_.set_state(Textfield::State::FOCUSED);

                    return Button::State::NORMAL;
                }

                if (!named_) {
                    std::string name = namechar_.get_text();

                    if (name.empty()) {
                        return Button::State::NORMAL;
                    }

                    if (name.size() >= 4) {
                        namechar_.set_state(Textfield::State::DISABLED);

                        buttons_[Buttons::BT_CHARC_OK]->set_state(
                            Button::State::DISABLED);
                        buttons_[Buttons::BT_CHARC_CANCEL]->set_state(
                            Button::State::DISABLED);

                        if (check_name(name)) {
                            fn_name_char(name);

                            return Button::State::IDENTITY;
                        }

                        std::function<void()> okhandler = [&]() {
                            namechar_.set_state(Textfield::State::FOCUSED);

                            buttons_[Buttons::BT_CHARC_OK]->set_state(
                                Button::State::NORMAL);
                            buttons_[Buttons::BT_CHARC_CANCEL]->set_state(
                                Button::State::NORMAL);
                        };

                        UI::get().emplace<UILoginNotice>(
                            UILoginNotice::Message::ILLEGAL_NAME,
                            okhandler);

                        return Button::State::NORMAL;
                    }

                    namechar_.set_state(Textfield::State::DISABLED);

                    buttons_[Buttons::BT_CHARC_OK]->set_state(
                        Button::State::DISABLED);
                    buttons_[Buttons::BT_CHARC_CANCEL]->set_state(
                        Button::State::DISABLED);

                    std::function<void()> okhandler = [&]() {
                        namechar_.set_state(Textfield::State::FOCUSED);

                        buttons_[Buttons::BT_CHARC_OK]->set_state(
                            Button::State::NORMAL);
                        buttons_[Buttons::BT_CHARC_CANCEL]->set_state(
                            Button::State::NORMAL);
                    };

                    UI::get().emplace<UILoginNotice>(
                        UILoginNotice::Message::ILLEGAL_NAME,
                        okhandler);

                    return Button::State::IDENTITY;
                }

                return Button::State::NORMAL;
            }
        case BT_BACK:
            Sound(Sound::Name::SCROLL_UP).play();

            UI::get().remove(UIElement::Type::CLASS_CREATION);

            if (auto charselect = UI::get().get_element<UICharSelect>()) {
                charselect->get().makeactive();
            }

            return Button::State::NORMAL;
        case Buttons::BT_CHARC_CANCEL:
            if (charset_) {
                charset_ = false;

                buttons_[Buttons::BT_CHARC_SKINL]->set_active(true);
                buttons_[Buttons::BT_CHARC_SKINR]->set_active(true);

                buttons_[Buttons::BT_CHARC_FACEL]->set_active(true);
                buttons_[Buttons::BT_CHARC_FACER]->set_active(true);
                buttons_[Buttons::BT_CHARC_HAIRL]->set_active(true);
                buttons_[Buttons::BT_CHARC_HAIRR]->set_active(true);
                buttons_[Buttons::BT_CHARC_TOPL]->set_active(true);
                buttons_[Buttons::BT_CHARC_TOPR]->set_active(true);
                buttons_[Buttons::BT_CHARC_SHOESL]->set_active(true);
                buttons_[Buttons::BT_CHARC_SHOESR]->set_active(true);
                buttons_[Buttons::BT_CHARC_WEPL]->set_active(true);
                buttons_[Buttons::BT_CHARC_WEPR]->set_active(true);

                for (size_t i = 0; i <= 7; i++) {
                    buttons_[Buttons::BT_CHARC_HAIRC0 + i]->set_active(true);
                }

                buttons_[Buttons::BT_CHARC_OK]->set_position(
                    Point<int16_t>(523, 425));
                buttons_[Buttons::BT_CHARC_CANCEL]->set_position(
                    Point<int16_t>(597, 425));

                namechar_.set_state(Textfield::State::DISABLED);

                return Button::State::NORMAL;
            } else {
                if (gender_) {
                    gender_ = false;

                    buttons_[Buttons::BT_CHARC_GENDER_M]->set_active(true);
                    buttons_[Buttons::BT_CHARC_GEMDER_F]->set_active(true);

                    buttons_[Buttons::BT_CHARC_SKINL]->set_active(false);
                    buttons_[Buttons::BT_CHARC_SKINR]->set_active(false);

                    buttons_[Buttons::BT_CHARC_FACEL]->set_active(false);
                    buttons_[Buttons::BT_CHARC_FACER]->set_active(false);
                    buttons_[Buttons::BT_CHARC_HAIRL]->set_active(false);
                    buttons_[Buttons::BT_CHARC_HAIRR]->set_active(false);
                    buttons_[Buttons::BT_CHARC_TOPL]->set_active(false);
                    buttons_[Buttons::BT_CHARC_TOPR]->set_active(false);
                    buttons_[Buttons::BT_CHARC_SHOESL]->set_active(false);
                    buttons_[Buttons::BT_CHARC_SHOESR]->set_active(false);
                    buttons_[Buttons::BT_CHARC_WEPL]->set_active(false);
                    buttons_[Buttons::BT_CHARC_WEPR]->set_active(false);

                    for (size_t i = 0; i <= 7; i++) {
                        buttons_[Buttons::BT_CHARC_HAIRC0 + i]->set_active(
                            false);
                    }

                    buttons_[Buttons::BT_CHARC_OK]->set_position(
                        Point<int16_t>(514, 394));
                    buttons_[Buttons::BT_CHARC_CANCEL]->set_position(
                        Point<int16_t>(590, 394));

                    return Button::State::NORMAL;
                }

                button_pressed(Buttons::BT_BACK);

                return Button::State::NORMAL;
            }
        case Buttons::BT_CHARC_FACEL:
            face_ = (face_ > 0) ? face_ - 1 : faces_[female_].size() - 1;
            newchar_.set_face(faces_[female_][face_]);
            facename_.change_text(newchar_.get_face()->get_name());

            return Button::State::NORMAL;
        case Buttons::BT_CHARC_FACER:
            face_ = (face_ < faces_[female_].size() - 1) ? face_ + 1 : 0;
            newchar_.set_face(faces_[female_][face_]);
            facename_.change_text(newchar_.get_face()->get_name());

            return Button::State::NORMAL;
        case Buttons::BT_CHARC_HAIRL:
            hair_ = (hair_ > 0) ? hair_ - 1 : hairs_[female_].size() - 1;
            newchar_.set_hair(hairs_[female_][hair_]
                              + haircolors_[female_][haircolor_]);
            hairname_.change_text(newchar_.get_hair()->get_name());

            return Button::State::NORMAL;
        case Buttons::BT_CHARC_HAIRR:
            hair_ = (hair_ < hairs_[female_].size() - 1) ? hair_ + 1 : 0;
            newchar_.set_hair(hairs_[female_][hair_]
                              + haircolors_[female_][haircolor_]);
            hairname_.change_text(newchar_.get_hair()->get_name());

            return Button::State::NORMAL;
        case Buttons::BT_CHARC_HAIRC0:
        case Buttons::BT_CHARC_HAIRC1:
        case Buttons::BT_CHARC_HAIRC2:
        case Buttons::BT_CHARC_HAIRC3:
        case Buttons::BT_CHARC_HAIRC4:
        case Buttons::BT_CHARC_HAIRC5:
        case Buttons::BT_CHARC_HAIRC6:
        case Buttons::BT_CHARC_HAIRC7:
            // TODO: These need to be changed so when you click the color it
            // only assigns the color, not the next in the series
            haircolor_ = (haircolor_ > 0) ? haircolor_ - 1
                                          : haircolors_[female_].size() - 1;
            newchar_.set_hair(hairs_[female_][hair_]
                              + haircolors_[female_][haircolor_]);

            return Button::State::NORMAL;
        case Buttons::BT_CHARC_SKINL:
            skin_ = (skin_ > 0) ? skin_ - 1 : skins_[female_].size() - 1;
            newchar_.set_body(skins_[female_][skin_]);
            bodyname_.change_text(newchar_.get_body()->get_name());

            return Button::State::NORMAL;
        case Buttons::BT_CHARC_SKINR:
            skin_ = (skin_ < skins_[female_].size() - 1) ? skin_ + 1 : 0;
            newchar_.set_body(skins_[female_][skin_]);
            bodyname_.change_text(newchar_.get_body()->get_name());

            return Button::State::NORMAL;
        case Buttons::BT_CHARC_TOPL:
            top_ = (top_ > 0) ? top_ - 1 : tops_[female_].size() - 1;
            newchar_.add_equip(tops_[female_][top_]);
            topname_.change_text(get_equipname(EquipSlot::Id::TOP));

            return Button::State::NORMAL;
        case Buttons::BT_CHARC_TOPR:
            top_ = (top_ < tops_[female_].size() - 1) ? top_ + 1 : 0;
            newchar_.add_equip(tops_[female_][top_]);
            topname_.change_text(get_equipname(EquipSlot::Id::TOP));

            return Button::State::NORMAL;
        case Buttons::BT_CHARC_BOTL:
            bot_ = (bot_ > 0) ? bot_ - 1 : bots_[female_].size() - 1;
            newchar_.add_equip(bots_[female_][bot_]);

            return Button::State::NORMAL;
        case Buttons::BT_CHARC_BOTR:
            bot_ = (bot_ < bots_[female_].size() - 1) ? bot_ + 1 : 0;
            newchar_.add_equip(bots_[female_][bot_]);

            return Button::State::NORMAL;
        case Buttons::BT_CHARC_SHOESL:
            shoe_ = (shoe_ > 0) ? shoe_ - 1 : shoes_[female_].size() - 1;
            newchar_.add_equip(shoes_[female_][shoe_]);
            shoename_.change_text(get_equipname(EquipSlot::Id::SHOES));

            return Button::State::NORMAL;
        case Buttons::BT_CHARC_SHOESR:
            shoe_ = (shoe_ < shoes_[female_].size() - 1) ? shoe_ + 1 : 0;
            newchar_.add_equip(shoes_[female_][shoe_]);
            shoename_.change_text(get_equipname(EquipSlot::Id::SHOES));

            return Button::State::NORMAL;
        case Buttons::BT_CHARC_WEPL:
            weapon_ =
                (weapon_ > 0) ? weapon_ - 1 : weapons_[female_].size() - 1;
            newchar_.add_equip(weapons_[female_][weapon_]);
            wepname_.change_text(get_equipname(EquipSlot::Id::WEAPON));

            return Button::State::NORMAL;
        case Buttons::BT_CHARC_WEPR:
            weapon_ =
                (weapon_ < weapons_[female_].size() - 1) ? weapon_ + 1 : 0;
            newchar_.add_equip(weapons_[female_][weapon_]);
            wepname_.change_text(get_equipname(EquipSlot::Id::WEAPON));

            return Button::State::NORMAL;
        case Buttons::BT_CHARC_GENDER_M:
            if (female_) {
                female_ = false;
                randomize_look();
            }

            return Button::State::NORMAL;
        case Buttons::BT_CHARC_GEMDER_F:
            if (!female_) {
                female_ = true;
                randomize_look();
            }

            return Button::State::NORMAL;
    }

    return Button::State::PRESSED;
}

void UIExplorerCreation::randomize_look() {
    hair_ = randomizer_.next_int(hairs_[female_].size());
    face_ = randomizer_.next_int(faces_[female_].size());
    skin_ = randomizer_.next_int(skins_[female_].size());
    haircolor_ = randomizer_.next_int(haircolors_[female_].size());
    top_ = randomizer_.next_int(tops_[female_].size());
    bot_ = 0;
    shoe_ = randomizer_.next_int(shoes_[female_].size());
    weapon_ = randomizer_.next_int(weapons_[female_].size());

    newchar_.set_body(skins_[female_][skin_]);
    newchar_.set_face(faces_[female_][face_]);
    newchar_.set_hair(hairs_[female_][hair_]
                      + haircolors_[female_][haircolor_]);
    newchar_.add_equip(tops_[female_][top_]);
    newchar_.add_equip(bots_[female_][bot_]);
    newchar_.add_equip(shoes_[female_][shoe_]);
    newchar_.add_equip(weapons_[female_][weapon_]);

    bodyname_.change_text(newchar_.get_body()->get_name());
    facename_.change_text(newchar_.get_face()->get_name());
    hairname_.change_text(newchar_.get_hair()->get_name());
    topname_.change_text(get_equipname(EquipSlot::Id::TOP));
    shoename_.change_text(get_equipname(EquipSlot::Id::SHOES));
    wepname_.change_text(get_equipname(EquipSlot::Id::WEAPON));
}

bool UIExplorerCreation::check_name(const std::string &name) const {
    img::node ForbiddenName = img::wz::etc["ForbiddenName.img"];

    for (const auto &forbiddenName : ForbiddenName) {
        std::string lName = to_lower(name);
        std::string fName = to_lower(forbiddenName);

        if (lName.find(fName) != std::string::npos) {
            return false;
        }
    }

    return true;
}

std::string UIExplorerCreation::to_lower(std::string value) const {
    std::transform(value.begin(), value.end(), value.begin(), ::tolower);

    return value;
}

const std::string &UIExplorerCreation::get_equipname(EquipSlot::Id slot) const {
    if (int32_t item_id = newchar_.get_equips().get_equip(slot)) {
        return ItemData::get(item_id).get_name();
    }

    static const std::string &nullstr = "Missing name.";

    return nullstr;
}
}  // namespace ms