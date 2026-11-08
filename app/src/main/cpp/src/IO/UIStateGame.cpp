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
#include "UIStateGame.h"

#include <functional>
#include <optional>

#include "../Net/Packets/GameplayPackets.h"
#include "OptionalCreator.h"
#include "UI.h"
#include "NativeChat.h"
#include "NativeNpcTalk.h"
#include "UITypes/UIBuffList.h"
#include "UITypes/UIChannel.h"
#include "UITypes/UICharInfo.h"
#include "UITypes/UIChat.h"
#include "UITypes/UIEquipInventory.h"
#include "UITypes/UIEvent.h"
#include "UITypes/UIGamepadConfig.h"
#include "UITypes/UIItemInventory.h"
#include "UITypes/UIMiniMap.h"
#include "UITypes/UIQuestLog.h"
#include "UITypes/UIQuit.h"
#include "UITypes/UIShop.h"
#include "UITypes/UISkillBook.h"
#include "UITypes/UIStatsInfo.h"
#include "UITypes/UIStatusBar.h"
#include "UITypes/UIStatusMessenger.h"
#include "UITypes/UIUserList.h"
#include "UITypes/UIWorldMap.h"

namespace ms {
    namespace {
        auto fn_enter_cashshop = []<typename... T>(T &&...args) {
            EnterCashShopPacket(std::forward<T>(args)...).dispatch();
        };

        // A preview is raised in the name of the window that owns it, but those owners are
        // spelled with Tooltip::Parent while hit-testing answers with UIElement::Type. The two
        // enums number their cases differently, so the owner has to be translated before
        // anything asks whether cursor and owner still agree; comparing the raw values made
        // every preview disagree with every window.
        UIElement::Type tooltip_owner(Tooltip::Parent parent) {
            switch (parent) {
                case Tooltip::Parent::EQUIP_INVENTORY:
                    return UIElement::Type::EQUIP_INVENTORY;
                case Tooltip::Parent::ITEM_INVENTORY:
                    return UIElement::Type::ITEM_INVENTORY;
                case Tooltip::Parent::SKILL_BOOK:
                    return UIElement::Type::SKILL_BOOK;
                case Tooltip::Parent::SHOP:
                    return UIElement::Type::SHOP;
                case Tooltip::Parent::EVENT:
                    return UIElement::Type::EVENT;
                case Tooltip::Parent::TEXT:
                    return UIElement::Type::QUIT;
                case Tooltip::Parent::KEY_CONFIG:
                    return UIElement::Type::KEY_CONFIG;
                case Tooltip::Parent::GAMEPAD_CONFIG:
                    return UIElement::Type::GAMEPAD_CONFIG;
                case Tooltip::Parent::WORLD_MAP:
                    return UIElement::Type::WORLD_MAP;
                case Tooltip::Parent::MINI_MAP:
                    return UIElement::Type::MINI_MAP;
                case Tooltip::Parent::NONE:
                default:
                    return UIElement::Type::NONE;
            }
        }
    }  // namespace

    UIStateGame::UIStateGame(uint8_t channel_count) :
            stats_(Stage::get().get_player().get_stats()),
            dragged_(nullptr),
            channel_count_(channel_count) {
        focused_ = UIElement::Type::NONE;
        tooltip_parent_ = Tooltip::Parent::NONE;
        tooltip_version_ = 0;
        native_version_ = 0;
        native_shown_ = false;

        const CharLook &look = Stage::get().get_player().get_look();
        const Inventory &inventory = Stage::get().get_player().get_inventory();

        emplace<UIStatusMessenger>();
        emplace<UIStatusBar>(stats_, channel_count_);
        emplace<UIMiniMap>(stats_);
        emplace<UIBuffList>();
        emplace<UIShop>(look, inventory);

        // The elements below place themselves against the interface's own screen, so that is the
        // size to remember and to hand out when it changes.
        VWIDTH = Constants::Constants::get().get_ui_viewwidth();
        VHEIGHT = Constants::Constants::get().get_ui_viewheight();

        NativeChat::activate();
        NativeTooltip::activate();
    }

    UIStateGame::~UIStateGame() {
        NativeChat::deactivate();
        NativeTooltip::deactivate();
        NativeNpcTalk::dismiss();
    }

    void UIStateGame::draw(float inter, Point<int16_t> cursor) const {
        for (const auto &type: element_order_) {
            const auto &element = elements_[type];

            if (element && element->is_active()) {
                element->draw(inter);
            }
        }

        // The item, equipment and skill previews are filled in by android's own views, so they are
        // written out once per change rather than once per frame: nothing about a preview differs
        // until the reader touches another icon or moves to another place. A preview that has not
        // been converted - the plain text and map name ones - is still painted by this pass.
        if (tooltip_) {
            const Tooltip &tooltip = tooltip_->get();
            // The preview stands where the piece that was touched stands, not under the finger: a
            // tap that lands a few pixels off centre used to move the whole window of words, and
            // the frame that carried it also made the piece being left behind arrive once more.
            Point<int16_t> position = tooltip_anchor_ + Point<int16_t>(0, 22);

            if (tooltip_version_ != native_version_ || position != native_position_) {
                NativeTooltip::List list;

                native_shown_ =
                        NativeTooltip::available() && tooltip.build(list, position);

                if (native_shown_) {
                    NativeTooltip::show(list);
                }
                else {
                    NativeTooltip::show(NativeTooltip::List());
                }

                native_version_ = tooltip_version_;
                native_position_ = position;
            }

            if (!native_shown_) {
                tooltip.draw(position);
            }
        }
        else if (native_shown_) {
            NativeTooltip::show(NativeTooltip::List());
            native_shown_ = false;
        }

        if (dragged_icon_) {
            dragged_icon_->get().dragdraw(cursor);
        }
    }

    void UIStateGame::update() {
        NativeChat::update();

        bool update_screen = false;
        int16_t new_width = Constants::Constants::get().get_ui_viewwidth();
        int16_t new_height = Constants::Constants::get().get_ui_viewheight();

        if (VWIDTH != new_width || VHEIGHT != new_height) {
            update_screen = true;
            VWIDTH = new_width;
            VHEIGHT = new_height;

            UI::get().remove(UIElement::Type::STATUS_BAR);

            const CharStats &stats = Stage::get().get_player().get_stats();
            emplace<UIStatusBar>(stats, channel_count_);
        }

        for (auto &type: element_order_) {
            auto &element = elements_[type];

            if (element && element->is_active()) {
                element->update();

                if (update_screen) {
                    element->update_screen(new_width, new_height);
                }
            }
        }
    }

    bool UIStateGame::drop_icon(const Icon &icon, Point<int16_t> pos) {
        // An icon that can become a key binding belongs to the quick-slot strip whenever it is let go
        // over one of its cells, empty or not. The strip is drawn at the bottom of the screen, where
        // any window can be laid over it, and a window's send_icon answers "taken" to everything it
        // has no cell for - which ended the drag and lost the binding without a trace.
        if (icon.binding()) {
            if (auto *bar = get(UIStatusBar::TYPE)) {
                auto *status = static_cast<UIStatusBar *>(bar);

                if (status->is_active() && status->over_quickslot(pos)) {
                    return status->send_icon(icon, pos);
                }
            }
        }

        if (UIElement *front = get_front(pos)) {
            return front->send_icon(icon, pos);
        }
        icon.drop_on_stage();

        return true;
    }

    void UIStateGame::remove_icon() {
        dragged_icon_->get().reset();
        dragged_icon_ = {};
    }

    void UIStateGame::doubleclick(Point<int16_t> pos) {
        if (UIElement *front = get_front(pos)) {
            front->doubleclick(pos);
            dragged_icon_ = {};  //Fix UIItemInventory crash when doubleclick equipment happens after dragging
        } else {
            Stage::get().doubleclick(pos);
        }
    }

    void UIStateGame::rightclick(Point<int16_t> pos) {
        if (UIElement *front = get_front(pos)) {
            front->rightclick(pos);
        }
    }

    void UIStateGame::send_key(KeyType::Id type,
                               int32_t action,
                               bool pressed,
                               bool escape) {
        if (UIElement *focusedelement = get(focused_)) {
            if (focusedelement->is_active()) {
                focusedelement->send_key(action, pressed, escape);

                // A second window can be holding the keyboard on top of the one that was asked, and
                // the release still belongs to the character: stopping him is the only thing a
                // let-go direction key does, and dropping it here leaves him running.
                const bool direction = action == KeyAction::Id::LEFT
                                       || action == KeyAction::Id::RIGHT
                                       || action == KeyAction::Id::UP
                                       || action == KeyAction::Id::DOWN;

                if (direction && !pressed
                    && type == KeyType::Id::ACTION) {
                    Stage::get().send_key(type, action, pressed);
                }

                return;
            }

            // The window that had the focus has gone away on its own rather than being closed, so
            // forgetting it is the whole response to that - the key itself still belongs to the
            // game. Returning here threw the first key of every session away, because a window that
            // is created at the same time as the screen and left closed - the shop - is what holds
            // the focus at that point.
            focused_ = UIElement::NONE;
        }

        switch (type) {
            case KeyType::Id::MENU:
                if (pressed) {
                    switch (action) {
                        case KeyAction::Id::EQUIPMENT:
                            emplace<UIEquipInventory>(
                                    Stage::get().get_player().get_inventory());
                            break;
                        case KeyAction::Id::ITEMS:
                            emplace<UIItemInventory>(
                                    Stage::get().get_player().get_inventory());
                            break;
                        case KeyAction::Id::STATS:
                            emplace<UIStatsInfo>(
                                    Stage::get().get_player().get_stats());
                            break;
                        case KeyAction::Id::SKILLS:
                            emplace<UISkillBook>(
                                    Stage::get().get_player().get_stats(),
                                    Stage::get().get_player().get_skills());
                            break;
                        case KeyAction::Id::FRIENDS:
                        case KeyAction::Id::PARTY:
                        case KeyAction::Id::BOSS_PARTY: {
                            UIUserList::Tab tab;

                            switch (action) {
                                case KeyAction::Id::FRIENDS:
                                    tab = UIUserList::Tab::FRIEND;
                                    break;
                                case KeyAction::Id::PARTY:
                                    tab = UIUserList::Tab::PARTY;
                                    break;
                                case KeyAction::Id::BOSS_PARTY:
                                    tab = UIUserList::Tab::BOSS;
                                    break;
                            }

                            auto userlist = UI::get().get_element<UIUserList>();

                            if (userlist && userlist->get().get_tab() != tab
                                && userlist->get().is_active()) {
                                userlist->get().change_tab(tab);
                            } else {
                                emplace<UIUserList>(tab);

                                if (userlist && userlist->get().get_tab() != tab
                                    && userlist->get().is_active()) {
                                    userlist->get().change_tab(tab);
                                }
                            }
                        }
                            break;
                        case KeyAction::Id::WORLD_MAP:
                            emplace<UIWorldMap>();
                            break;
                        case KeyAction::Id::MAPLE_CHAT: {
                            auto chat = UI::get().get_element<UIChat>();

                            if (!chat || !chat->get().is_active()) {
                                emplace<UIChat>();
                            }
                        }
                            break;
                        case KeyAction::Id::MINI_MAP:
                            if (auto minimap = UI::get().get_element<UIMiniMap>()) {
                                minimap->get().send_key(action, pressed, escape);
                            }

                            break;
                        case KeyAction::Id::QUEST_LOG:
                            emplace<UIQuestLog>(
                                    Stage::get().get_player().get_quests());
                            break;
                        case KeyAction::Id::MENU:
                            if (auto statusbar =
                                    UI::get().get_element<UIStatusBar>()) {
                                statusbar->get().toggle_menu();
                            }

                            break;
                        case KeyAction::Id::QUICK_SLOTS:
                            if (auto statusbar =
                                    UI::get().get_element<UIStatusBar>()) {
                                statusbar->get().toggle_qs();
                            }

                            break;
                        case KeyAction::Id::CASHSHOP:
                            fn_enter_cashshop();
                            break;
                        case KeyAction::Id::TOGGLE_CHAT:
                            // The message window owns the keyboard now, so there is nothing
                            // left on the game side for this key to open.
                            break;
                        case KeyAction::Id::KEY_BINDINGS: {
                            // The one function that opens the controller editor. This device is
                            // driven by a pad and by touch, so the key it can actually reach is the
                            // one whose table matches what it presses; the keyboard editor is still
                            // there behind 菜单 → 设定 → 按键设置.
                            auto gamepad =
                                UI::get().get_element<UIGamepadConfig>();

                            if (!gamepad || !gamepad->get().is_active()) {
                                emplace<UIGamepadConfig>(
                                        Stage::get().get_player().get_inventory(),
                                        Stage::get().get_player().get_skills());
                            } else if (gamepad && gamepad->get().is_active()) {
                                gamepad->get().close();
                            }

                            break;
                        }
                        case KeyAction::Id::MAIN_MENU:
                            if (auto statusbar =
                                    UI::get().get_element<UIStatusBar>()) {
                                statusbar->get().send_key(action, pressed, escape);
                            }

                            break;
                        case KeyAction::Id::EVENT:
                            emplace<UIEvent>();
                            break;
                        case KeyAction::Id::CHANGE_CHANNEL:
                            emplace<UIChannel>(
                                    Stage::get().get_player().get_world_id(),
                                    Stage::get().get_player().get_channel_id(),
                                    channel_count_);
                            break;
                        default:
                            std::cout << "Action (" << action << ") not handled!"
                                      << std::endl;
                            break;
                    }
                }
                break;
            case KeyType::Id::ACTION:
            case KeyType::Id::FACE:
            case KeyType::Id::ITEM:
            case KeyType::Id::SKILL:
                Stage::get().send_key(type, action, pressed);
                break;
        }
    }

    Cursor::State UIStateGame::send_cursor(Cursor::State cursorstate,
                                           Point<int16_t> cursorpos) {
        if (dragged_icon_) {
            if (cursorstate == Cursor::State::CLICKING) {
                if (drop_icon(*dragged_icon_, cursorpos)) {
                    remove_icon();
                    time_rel_grabbed = std::chrono::steady_clock::now();
                }
                return cursorstate;
            }
            return Cursor::State::GRABBING;
        }
        bool clicked = cursorstate == Cursor::State::CLICKING
                       || cursorstate == Cursor::State::VSCROLL_IDLE;

        // A preview raised for the native window carries at most one control, and it stands wherever
        // the panel falls - over the window that raised it, over another window, over the map. So the
        // press has to be read here, before the cursor is handed to whatever lies under it: asked
        // there, the same tap would be taken for a row of the shop, and the row would put the preview
        // down again before the control was ever reached.
        if (tooltip_) {
            Tooltip &tooltip = tooltip_->get();

            if (clicked) {
                if (tooltip.press_action(cursorpos)) {
                    NativeChat::sink();

                    // The control has said what it came to say and the preview it was read from has
                    // no reason to stay. The one that pressed it does not clear it itself - it would
                    // be destroying the call it is still inside of.
                    clear_tooltip(tooltip_parent_);
                    return Cursor::State::IDLE;
                }
            }
            else if (tooltip.action_at(cursorpos)) {
                return Cursor::State::CAN_CLICK;
            }
        }

        if (auto *focusedelement = get(focused_)) {
            if (focusedelement->is_active()) {
                if (clicked) {
                    // The press belongs to a window of the interface, so the message window - which
                    // cannot be sent under the interface for real, only faded and made to let touches
                    // through - has no business being the first thing the eye comes to.
                    NativeChat::sink();
                }
                return focusedelement->send_cursor(clicked, cursorpos);
            }

            // As with the key above: the window that owned the cursor has gone, but the tap still
            // means what it means. Dropping it here cost the first tap of every session too.
            focused_ = UIElement::Type::NONE;
        }

        if (!clicked) {
            dragged_ = nullptr;

            auto *front = get_front(cursorpos);
            UIElement::Type front_type =
                    front ? front->get_type() : UIElement::Type::NONE;

            // A tooltip must vanish as soon as the cursor is no longer over the window that
            // owns it — including when the cursor moves out to empty map space (front == null),
            // which is how a tap on the map dismisses an item preview.
            if (tooltip_parent_ != Tooltip::Parent::NONE
                && front_type != tooltip_owner(tooltip_parent_)) {
                clear_tooltip(tooltip_parent_);
            }

            if (front) {
                return front->send_cursor(clicked, cursorpos);
            }
            return Stage::get().send_cursor(clicked, cursorpos);
        }
        if (!dragged_) {
            UIElement::Type drag_element_type = UIElement::Type::NONE;

            for (auto iter = element_order_.rbegin(); iter != element_order_.rend();
                 ++iter) {
                auto &element = elements_[*iter];

                if (element && element->is_active()
                    && element->is_in_range(cursorpos)) {
                    dragged_ = element.get();
                    drag_element_type = *iter;
                    break;
                }
            }

            if (drag_element_type != UIElement::Type::NONE) {
                element_order_.remove(drag_element_type);
                element_order_.push_back(drag_element_type);
            }
        }

        // fixes bug with icon in keyconfig being re-grabbed after
        // assign
        if (auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - time_rel_grabbed);
                duration < MIN_DELAY_NEXT_GRAB_) {
            return Cursor::State::IDLE;
        }

        if (dragged_) {
            // A window of the interface took the press, which is the reader working in it rather than
            // reading what came past: the message window goes behind.
            NativeChat::sink();

            return dragged_->send_cursor(clicked, cursorpos);
        }

        // Nothing of the interface wanted the press, so the last thing that can claim it is the
        // message window - which sits under the interface and over the map while it is out of the way.
        // Claiming it here is the reader asking for the window back, and the map underneath does not
        // get to read the same press as somewhere to walk to.
        if (NativeChat::covers(cursorpos.x(), cursorpos.y())) {
            NativeChat::raise();

            return Cursor::State::IDLE;
        }

        return Stage::get().send_cursor(clicked, cursorpos);
    }

    bool UIStateGame::cursor_in_drag_area(Point<int16_t> pos) {
        if (auto *front = get_front(pos)) {
            return front->in_drag_range(pos);
        }

        return false;
    }

    void UIStateGame::send_scroll(double yoffset) {
        // A swipe belongs to the window under the finger, not to every open window at once.
        if (auto *front = get_front(UI::get().get_cursor_position())) {
            front->send_scroll(yoffset);
        }
    }

    void UIStateGame::send_close() {
        UI::get().emplace<UIQuit>(stats_);
    }

    void UIStateGame::drag_icon(Icon *drgic) {
        dragged_icon_ = create_optional<Icon>(drgic);
    }

    void UIStateGame::cancel_drag() {
        if (dragged_icon_) {
            remove_icon();
        }
    }

    void UIStateGame::clear_tooltip(Tooltip::Parent parent) {
        if (parent == tooltip_parent_) {
            eq_tooltip_.set_equip(Tooltip::Parent::NONE, 0);
            it_tooltip_.set_item(0);
            te_tooltip_.set_text("");
            ma_tooltip_.reset();
            tooltip_ = {};
            tooltip_parent_ = Tooltip::Parent::NONE;
            tooltip_version_++;
        }
    }

    void UIStateGame::clear_tooltip() {
        clear_tooltip(tooltip_parent_);
    }

    bool UIStateGame::has_tooltip(Tooltip::Parent parent) const {
        return parent != Tooltip::Parent::NONE && parent == tooltip_parent_;
    }

    void UIStateGame::show_equip(Tooltip::Parent parent,
                                 int16_t slot,
                                 Point<int16_t> anchor) {
        eq_tooltip_.set_equip(parent, slot);

        if (slot) {
            tooltip_ = eq_tooltip_;
            tooltip_parent_ = parent;
            tooltip_anchor_ = anchor;
        }

        tooltip_version_++;
    }

    void UIStateGame::show_item(Tooltip::Parent parent,
                                int32_t itemid,
                                Point<int16_t> anchor) {
        it_tooltip_.set_item(itemid);

        if (itemid) {
            tooltip_ = it_tooltip_;
            tooltip_parent_ = parent;
            tooltip_anchor_ = anchor;
        }

        tooltip_version_++;
    }

    void UIStateGame::show_goods(Tooltip::Parent parent,
                                 int32_t item_id,
                                 Point<int16_t> anchor) {
        eq_tooltip_.set_goods(parent, item_id);

        if (item_id) {
            tooltip_ = eq_tooltip_;
            tooltip_parent_ = parent;
            tooltip_anchor_ = anchor;
        }

        tooltip_version_++;
    }

    void UIStateGame::set_tooltip_action(std::string label,
                                         std::function<void()> on_pressed) {
        if (!tooltip_) {
            return;
        }

        // The control is recorded while the preview is written out, so asking for it means the
        // preview has to be written out again - otherwise it would never reach the screen, and its
        // place on the screen would never be known well enough to be pressed.
        tooltip_->get().set_action(std::move(label), std::move(on_pressed));
        tooltip_version_++;
    }

    void UIStateGame::show_skill(Tooltip::Parent parent,
                                 int32_t skill_id,
                                 int32_t level,
                                 int32_t masterlevel,
                                 int64_t expiration,
                                 Point<int16_t> anchor) {
        sk_tooltip_.set_skill(skill_id, level, masterlevel, expiration);

        if (skill_id) {
            tooltip_ = sk_tooltip_;
            tooltip_parent_ = parent;
            tooltip_anchor_ = anchor;
        }

        tooltip_version_++;
    }

    void UIStateGame::show_text(Tooltip::Parent parent,
                                std::string text,
                                Point<int16_t> anchor) {
        te_tooltip_.set_text(text);

        if (!text.empty()) {
            tooltip_ = te_tooltip_;
            tooltip_parent_ = parent;
            tooltip_anchor_ = anchor;
        }

        tooltip_version_++;
    }

    void UIStateGame::show_map(Tooltip::Parent parent,
                               std::string name,
                               std::string description,
                               int32_t mapid,
                               bool bolded,
                               Point<int16_t> anchor) {
        ma_tooltip_.set_name(parent, name, bolded);
        ma_tooltip_.set_desc(description);
        ma_tooltip_.set_mapid(mapid);

        if (!name.empty()) {
            tooltip_ = ma_tooltip_;
            tooltip_parent_ = parent;
            tooltip_anchor_ = anchor;
        }

        tooltip_version_++;
    }

    template<class T, typename... Args>
    void UIStateGame::emplace(Args &&...args) {
        if (auto iter = pre_add(T::TYPE, T::TOGGLED, T::FOCUSED)) {
            (*iter).second = std::make_unique<T>(std::forward<Args>(args)...);

            auto silent_types = {UIElement::Type::STATUS_MESSENGER,
                                 UIElement::Type::STATUS_BAR,
                                 UIElement::Type::MINI_MAP,
                                 UIElement::Type::BUFF_LIST,
                                 UIElement::Type::NPC_TALK,
                                 UIElement::Type::SHOP};

            if (std::find(silent_types.begin(), silent_types.end(), T::TYPE)
                == silent_types.end()) {
                // A window of the interface has just come over the map, and a window that came by
                // itself is not one the reader is still reading messages through. The silent types are
                // the parts of the interface the game keeps laid out at the edges of the screen, and
                // putting one of them back is not the reader asking for anything.
                NativeChat::sink();

                if (T::TYPE == UIElement::Type::WORLD_MAP) {
                    Sound(Sound::Name::WORLD_MAP_OPEN).play();
                } else {
                    Sound(Sound::Name::MENU_UP).play();
                }

                UI::get().send_cursor(false);
            }
        }
    }

    UIState::Iterator UIStateGame::pre_add(UIElement::Type type,
                                           bool is_toggled,
                                           bool is_focused) {
        auto &element = elements_[type];

        if (element && is_toggled) {
            element_order_.remove(type);
            element_order_.push_back(type);

            bool active = element->is_active();

            element->toggle_active();

            if (active != element->is_active()) {
                if (element->is_active()) {
                    if (type == UIElement::Type::WORLD_MAP) {
                        Sound(Sound::Name::WORLD_MAP_OPEN).play();
                    } else {
                        Sound(Sound::Name::MENU_UP).play();
                    }

                    UI::get().send_cursor(false);
                } else {
                    if (type == UIElement::Type::WORLD_MAP) {
                        Sound(Sound::Name::WORLD_MAP_CLOSE).play();
                    } else {
                        Sound(Sound::Name::MENU_DOWN).play();
                    }

                    if (dragged_icon_) {
                        if (element->get_type()
                            == icon_map_[dragged_icon_->get().get_type()]) {
                            remove_icon();
                        }
                    }

                    UI::get().send_cursor(false);
                }
            }

            return elements_.end();
        }
        remove(type);
        element_order_.push_back(type);

        if (is_focused) {
            focused_ = type;
        }

        return elements_.find(type);
    }

    void UIStateGame::remove(UIElement::Type type) {
        if (type == focused_) {
            focused_ = UIElement::Type::NONE;
        }

        if (type == tooltip_owner(tooltip_parent_)) {
            clear_tooltip(tooltip_parent_);
        }

        element_order_.remove(type);

        if (auto &element = elements_[type]) {
            element->deactivate();
            element.release();
        }
    }

    UIElement *UIStateGame::get(UIElement::Type type) {
        return elements_[type].get();
    }

    UIElement *UIStateGame::get_front(std::list<UIElement::Type> types) {
        auto begin = element_order_.rbegin();
        auto end = element_order_.rend();

        for (auto iter = begin; iter != end; ++iter) {
            if (std::find(types.begin(), types.end(), *iter) != types.end()) {
                auto &element = elements_[*iter];

                if (element && element->is_active()) {
                    return element.get();
                }
            }
        }

        return nullptr;
    }
// TODO: (rich) fix

    UIElement *UIStateGame::get_front(Point<int16_t> pos) {
        // A cell that is showing an icon is part of the HUD, so it answers the finger before any
        // window laid over it does. Without this, opening a window over the bottom of the screen put
        // its own widgets in front of the strip and a slot could no longer be picked up.
        if (auto *bar = get(UIStatusBar::TYPE)) {
            auto *status = static_cast<UIStatusBar *>(bar);

            if (status->is_active() && status->over_filled_quickslot(pos)) {
                return status;
            }
        }

        auto begin = element_order_.rbegin();
        auto end = element_order_.rend();

        for (auto iter = begin; iter != end; ++iter) {
            auto &element = elements_[*iter];

            if (element && element->is_active() && element->is_in_range(pos)) {
                return element.get();
            }
        }

        return nullptr;
    }
}  // namespace ms