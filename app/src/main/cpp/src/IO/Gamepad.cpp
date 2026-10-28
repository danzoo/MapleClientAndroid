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
#include "Gamepad.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "../Constants.h"
#include "KeyType.h"
#include "Stage.h"
#include "UI.h"
#include "UIStateGame.h"
#include "glfm.h"

namespace ms {
    namespace {
        std::optional<Gamepad::Button> to_button(int32_t keycode) {
            switch (keycode) {
                case GLFMKeyCodeGamepadA:
                    return Gamepad::Button::A;
                case GLFMKeyCodeGamepadB:
                    return Gamepad::Button::B;
                case GLFMKeyCodeGamepadX:
                    return Gamepad::Button::X;
                case GLFMKeyCodeGamepadY:
                    return Gamepad::Button::Y;
                case GLFMKeyCodeGamepadLeftShoulder:
                    return Gamepad::Button::LEFT_SHOULDER;
                case GLFMKeyCodeGamepadRightShoulder:
                    return Gamepad::Button::RIGHT_SHOULDER;
                case GLFMKeyCodeGamepadLeftTrigger:
                    return Gamepad::Button::LEFT_TRIGGER;
                case GLFMKeyCodeGamepadRightTrigger:
                    return Gamepad::Button::RIGHT_TRIGGER;
                case GLFMKeyCodeGamepadSelect:
                    return Gamepad::Button::SELECT;
                case GLFMKeyCodeGamepadStart:
                    return Gamepad::Button::START;
                case GLFMKeyCodeGamepadLeftStick:
                    return Gamepad::Button::LEFT_STICK;
                case GLFMKeyCodeGamepadRightStick:
                    return Gamepad::Button::RIGHT_STICK;
                case GLFMKeyCodeGamepadMode:
                    return Gamepad::Button::MODE;
                default:
                    return std::nullopt;
            }
        }
    }  // namespace

    uint32_t Gamepad::bit(Button button) {
        return 1u << static_cast<uint8_t>(button);
    }

    bool Gamepad::is_connected() const {
        return connected_;
    }

    bool Gamepad::on_key(int32_t keycode, bool pressed) {
        const auto button = to_button(keycode);

        if (!button) {
            return false;
        }

        const uint32_t held = bit(*button);
        connect_expiry_ = clock::now() + CONNECT_TIMEOUT;
        connected_ = true;

        if (pressed) {
            // Android repeats a held button; only the first report is an edge.
            if (state_ & held) {
                return true;
            }

            state_ |= held;
            press(*button);
        }
        else {
            if (!(state_ & held)) {
                return true;
            }

            state_ &= ~held;
            release(*button);
        }

        return true;
    }

    void Gamepad::press(Button button) {
        const bool in_game = UI::get().get_state_type() == typeid(UIStateGame);
        const bool modal = in_game && UI::get().is_modal_active();
        const auto now = clock::now();

        switch (button) {
            case Button::A:
                // A jumps, and confirms while a dialog or the menu is open.
                confirm_held_ = modal;

                if (modal) {
                    UI::get().send_key(GLFMKeyCodeEnter, true);
                }
                else if (in_game) {
                    Stage::get().send_key(KeyType::Id::ACTION, KeyAction::Id::JUMP, true);
                }
                break;
            case Button::B:
                // B attacks, and cancels while a dialog or the menu is open.
                if (modal) {
                    cancel_held_ = true;
                    UI::get().send_key(GLFMKeyCodeEscape, true);
                }
                else if (in_game) {
                    tap(KeyAction::Id::ATTACK);
                    attack_repeat_ = now;
                }
                break;
            case Button::X:
                if (in_game) {
                    Stage::get().get_npcs().talk_to_nearest(
                            Stage::get().get_player().get_position(),
                            Constants::TALK_RANGE);
                    talk_repeat_ = now;
                }
                break;
            case Button::Y:
                if (in_game) {
                    Stage::get().pick_up_near(Constants::PICKUP_RANGE);
                    pickup_repeat_ = now;
                }
                break;
            case Button::SELECT:
                UI::get().send_key(GLFMKeyCodeEscape, true);
                break;
            case Button::START:
                UI::get().send_action(KeyAction::Id::MENU);
                break;
            case Button::LEFT_SHOULDER:
                UI::get().send_action(KeyAction::Id::ITEMS);
                break;
            case Button::RIGHT_SHOULDER:
                UI::get().send_action(KeyAction::Id::SKILLS);
                break;
            case Button::LEFT_TRIGGER:
                UI::get().send_action(KeyAction::Id::STATS);
                break;
            case Button::RIGHT_TRIGGER:
                UI::get().send_action(KeyAction::Id::QUICK_SLOTS);
                break;
            case Button::LEFT_STICK:
                UI::get().send_action(KeyAction::Id::WORLD_MAP);
                break;
            case Button::RIGHT_STICK:
                UI::get().send_action(KeyAction::Id::MINI_MAP);
                break;
            case Button::MODE:
                break;
        }
    }

    void Gamepad::release(Button button) {
        const bool in_game = UI::get().get_state_type() == typeid(UIStateGame);

        switch (button) {
            case Button::A:
                if (confirm_held_) {
                    UI::get().send_key(GLFMKeyCodeEnter, false);
                }
                else if (in_game) {
                    Stage::get().send_key(KeyType::Id::ACTION, KeyAction::Id::JUMP, false);
                }

                confirm_held_ = false;
                break;
            case Button::B:
                if (cancel_held_) {
                    UI::get().send_key(GLFMKeyCodeEscape, false);
                    cancel_held_ = false;
                }
                break;
            case Button::SELECT:
                UI::get().send_key(GLFMKeyCodeEscape, false);
                break;
            default:
                break;
        }
    }

    void Gamepad::update() {
        if (!connected_) {
            return;
        }

        const auto now = clock::now();

        // A stick pushed to a constant reading reports nothing more, so silence while something is
        // still engaged isn't taken for the controller having gone away.
        if (now > connect_expiry_) {
            if (stick_dirs_ == NONE && state_ == 0) {
                disconnect();
                return;
            }

            connect_expiry_ = now + CONNECT_TIMEOUT;
        }

        if (UI::get().get_state_type() != typeid(UIStateGame)) {
            return;
        }

        // Held buttons keep their action going; Android doesn't report them again.
        if ((state_ & bit(Button::B)) && !cancel_held_
            && now - attack_repeat_ >= ATTACK_REPEAT) {
            tap(KeyAction::Id::ATTACK);
            attack_repeat_ = now;
        }

        if ((state_ & bit(Button::Y)) && now - pickup_repeat_ >= PICKUP_REPEAT) {
            Stage::get().pick_up_near(Constants::PICKUP_RANGE);
            pickup_repeat_ = now;
        }

        if ((state_ & bit(Button::X)) && now - talk_repeat_ >= TALK_REPEAT) {
            Stage::get().get_npcs().talk_to_nearest(
                    Stage::get().get_player().get_position(),
                    Constants::TALK_RANGE);
            talk_repeat_ = now;
        }
    }

    void Gamepad::tap(KeyAction::Id action) {
        Stage::get().send_key(KeyType::Id::ACTION, action, true);
        Stage::get().send_key(KeyType::Id::ACTION, action, false);
    }

    void Gamepad::set_stick(double x, double y) {
        const double axis_x = std::clamp(x, -1.0, 1.0);
        const double axis_y = std::clamp(y, -1.0, 1.0);

        // A controller reports its resting stick once when it connects, which must not take the
        // on-screen controls away from a player using touch.
        if (!connected_ && std::abs(axis_x) <= STICK_DEADZONE
            && std::abs(axis_y) <= STICK_DEADZONE) {
            return;
        }

        connected_ = true;
        connect_expiry_ = clock::now() + CONNECT_TIMEOUT;

        // The game moves in four directions only, so the axis the stick is furthest along owns
        // the direction and the other one is treated as centered. That puts the boundary between
        // two directions at 45 degrees instead of letting a diagonal push send both keys, which
        // the game reads as climbing a rope or lying down.
        const double magnitude_x = std::abs(axis_x);
        const double magnitude_y = std::abs(axis_y);
        const bool use_x = std::abs(magnitude_x - magnitude_y) < STICK_AXIS_MARGIN
                           ? (stick_dirs_ & (LEFT | RIGHT)) != 0
                           : magnitude_x > magnitude_y;
        const double push_x = use_x ? axis_x : 0.0;
        const double push_y = use_x ? 0.0 : axis_y;

        // A stick held still reports nothing until it moves again, so directions stay engaged
        // until an axis comes back inside STICK_RETURN. Reading a little above the deadzone
        // keeps the direction, since axes commonly jitter by a percent or two around it.
        uint8_t dirs = stick_dirs_;

        if (push_x < -STICK_DEADZONE) {
            dirs = static_cast<uint8_t>((dirs & ~RIGHT) | LEFT);
        }
        else if (push_x > STICK_DEADZONE) {
            dirs = static_cast<uint8_t>((dirs & ~LEFT) | RIGHT);
        }
        else if (std::abs(push_x) < STICK_RETURN) {
            dirs &= ~(LEFT | RIGHT);
        }

        if (push_y < -STICK_DEADZONE) {
            dirs = static_cast<uint8_t>((dirs & ~DOWN) | UP);
        }
        else if (push_y > STICK_DEADZONE) {
            dirs = static_cast<uint8_t>((dirs & ~UP) | DOWN);
        }
        else if (std::abs(push_y) < STICK_RETURN) {
            dirs &= ~(UP | DOWN);
        }

        if (dirs == stick_dirs_) {
            return;
        }

        const uint8_t changed = dirs ^ stick_dirs_;
        const struct {
            uint8_t dir;
            int32_t key;
        } directions[] = {
            {LEFT, GLFMKeyCodeArrowLeft},
            {RIGHT, GLFMKeyCodeArrowRight},
            {UP, GLFMKeyCodeArrowUp},
            {DOWN, GLFMKeyCodeArrowDown},
        };

        // Releases go out before presses. Every state reacts only to a press, and a press landing
        // while the old direction still has the character prone or climbing is dropped, which would
        // leave the character standing still until the stick was centred and pushed again.
        for (const auto &direction : directions) {
            if (changed & direction.dir) {
                UI::get().send_key(direction.key, false);
            }
        }

        for (const auto &direction : directions) {
            if (changed & direction.dir) {
                UI::get().send_key(direction.key, (dirs & direction.dir) != 0);
            }
        }

        stick_dirs_ = dirs;
    }

    void Gamepad::clear_directions() {
        if (stick_dirs_ == NONE) {
            return;
        }

        stick_dirs_ = NONE;
        UI::get().send_key(GLFMKeyCodeArrowLeft, false);
        UI::get().send_key(GLFMKeyCodeArrowRight, false);
        UI::get().send_key(GLFMKeyCodeArrowUp, false);
        UI::get().send_key(GLFMKeyCodeArrowDown, false);
    }

    void Gamepad::disconnect() {
        connected_ = false;
        state_ = 0;
        confirm_held_ = false;
        cancel_held_ = false;
        clear_directions();
    }
}  // namespace ms
