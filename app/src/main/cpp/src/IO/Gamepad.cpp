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
#include "GamepadConfig.h"
#include "KeyType.h"
#include "Stage.h"
#include "UI.h"
#include "UIStateGame.h"
#include "glfm.h"

namespace ms {
    namespace {
        // Constants.h holds these as the number of milliseconds they each measure; the controller's
        // whole notion of time is steady_clock, so they are turned into durations once here rather
        // than at every use.
        constexpr auto CONNECT_TIMEOUT =
                std::chrono::milliseconds(Constants::GAMEPAD_CONNECT_TIMEOUT_MS);
        constexpr auto WALK_REPEAT = std::chrono::milliseconds(Constants::GAMEPAD_WALK_REPEAT_MS);
        constexpr auto ATTACK_REPEAT =
                std::chrono::milliseconds(Constants::GAMEPAD_ATTACK_REPEAT_MS);
        constexpr auto PICKUP_REPEAT =
                std::chrono::milliseconds(Constants::GAMEPAD_PICKUP_REPEAT_MS);
        constexpr auto TALK_REPEAT = std::chrono::milliseconds(Constants::GAMEPAD_TALK_REPEAT_MS);

        // The four directions in the order a stick is walked through them, low bit first.
        constexpr Gamepad::Direction kDirections[4] = {
            Gamepad::Direction::LEFT,
            Gamepad::Direction::RIGHT,
            Gamepad::Direction::UP,
            Gamepad::Direction::DOWN,
        };

        // Which slot a raw controller code belongs to. A code with no place on the binding window
        // - the controller's home key - has no slot and so no binding.
        std::optional<GamepadConfig::Slot> to_slot(int32_t keycode) {
            using Slot = GamepadConfig::Slot;

            switch (keycode) {
                case GLFMKeyCodeGamepadA:
                    return Slot::A;
                case GLFMKeyCodeGamepadB:
                    return Slot::B;
                case GLFMKeyCodeGamepadX:
                    return Slot::X;
                case GLFMKeyCodeGamepadY:
                    return Slot::Y;
                case GLFMKeyCodeGamepadLeftShoulder:
                    return Slot::L1;
                case GLFMKeyCodeGamepadRightShoulder:
                    return Slot::R1;
                case GLFMKeyCodeGamepadLeftTrigger:
                    return Slot::L2;
                case GLFMKeyCodeGamepadRightTrigger:
                    return Slot::R2;
                case GLFMKeyCodeGamepadSelect:
                    return Slot::SELECT;
                case GLFMKeyCodeGamepadStart:
                    return Slot::START;
                case GLFMKeyCodeGamepadLeftStick:
                    return Slot::LS_PRESS;
                case GLFMKeyCodeGamepadRightStick:
                    return Slot::RS_PRESS;
                case GLFMKeyCodeGamepadDpadUp:
                    return Slot::DPAD_UP;
                case GLFMKeyCodeGamepadDpadRight:
                    return Slot::DPAD_RIGHT;
                case GLFMKeyCodeGamepadDpadDown:
                    return Slot::DPAD_DOWN;
                case GLFMKeyCodeGamepadDpadLeft:
                    return Slot::DPAD_LEFT;
                default:
                    return std::nullopt;
            }
        }
    }  // namespace

    uint32_t Gamepad::bit(GamepadConfig::Slot slot) {
        return 1u << static_cast<uint8_t>(slot);
    }

    bool Gamepad::on_key(int32_t keycode, bool pressed) {
        const auto slot = to_slot(keycode);

        if (!slot) {
            return false;
        }

        const uint32_t held = bit(*slot);
        connect_expiry_ = clock::now() + CONNECT_TIMEOUT;
        connected_ = true;

        if (pressed) {
            // Android repeats a held button; only the first report is an edge.
            if (state_ & held) {
                return true;
            }

            state_ |= held;
            press(*slot);
        }
        else {
            if (!(state_ & held)) {
                return true;
            }

            state_ &= ~held;
            release(*slot);
        }

        return true;
    }

    Gamepad::Direction Gamepad::to_direction(int32_t action) {
        switch (action) {
            case KeyAction::Id::LEFT:
                return Direction::LEFT;
            case KeyAction::Id::RIGHT:
                return Direction::RIGHT;
            case KeyAction::Id::UP:
                return Direction::UP;
            case KeyAction::Id::DOWN:
                return Direction::DOWN;
            default:
                return Direction::NONE;
        }
    }

    GamepadConfig::Slot Gamepad::stick_slot(int stick, Direction direction) {
        const bool left = stick == 0;

        switch (direction) {
            case Direction::LEFT:
                return left ? GamepadConfig::Slot::LS_LEFT
                            : GamepadConfig::Slot::RS_LEFT;
            case Direction::RIGHT:
                return left ? GamepadConfig::Slot::LS_RIGHT
                            : GamepadConfig::Slot::RS_RIGHT;
            case Direction::UP:
                return left ? GamepadConfig::Slot::LS_UP : GamepadConfig::Slot::RS_UP;
            case Direction::DOWN:
                return left ? GamepadConfig::Slot::LS_DOWN : GamepadConfig::Slot::RS_DOWN;
            default:
                return GamepadConfig::Slot::LENGTH;
        }
    }

    void Gamepad::press(GamepadConfig::Slot slot) {
        const Keyboard::Mapping mapping = GamepadConfig::get().mapping(slot);

        if (mapping.type == KeyType::Id::NONE) {
            return;
        }

        const bool in_game = UI::get().get_state_type() == typeid(UIStateGame);
        const bool modal = in_game && UI::get().is_modal_active();

        if (mapping.type == KeyType::Id::ACTION) {
            const Direction direction = to_direction(mapping.action);

            if (direction != Direction::NONE) {
                press_direction(direction, slot);
                return;
            }
        }

        // Jumping and attacking are how a player answers a dialog, so while one owns the screen
        // those two bindings answer it instead of doing their own job.
        if (modal && mapping.type == KeyType::Id::ACTION
            && mapping.action == KeyAction::Id::JUMP) {
            confirm_ |= bit(slot);
            UI::get().send_key(GLFMKeyCodeEnter, true);
            return;
        }

        if (modal && mapping.type == KeyType::Id::ACTION
            && mapping.action == KeyAction::Id::ATTACK) {
            cancel_ |= bit(slot);
            UI::get().send_key(GLFMKeyCodeEscape, true);
            return;
        }

        if (mapping.action == KeyAction::Id::ATTACK
            || mapping.action == KeyAction::Id::PICKUP
            || mapping.action == KeyAction::Id::INTERACT_HARVEST) {
            // These three are what a controller is actually held down for, and each is something
            // you do to the world rather than to a window, so they are carried out here and kept
            // going by update() rather than left to a key Android only reports once.
            if (!in_game) {
                return;
            }

            if (mapping.action == KeyAction::Id::ATTACK) {
                tap(KeyAction::Id::ATTACK);
                attack_repeat_ = clock::now();
            }
            else if (mapping.action == KeyAction::Id::PICKUP) {
                Stage::get().pick_up_near(Constants::PICKUP_RANGE);
                pickup_repeat_ = clock::now();
            }
            else {
                Stage::get().get_npcs().talk_to_nearest(
                        Stage::get().get_player().get_position(),
                        Constants::TALK_RANGE);
                talk_repeat_ = clock::now();
            }

            return;
        }

        UI::get().send_key(GamepadConfig::keycode(slot), true);
    }

    void Gamepad::release(GamepadConfig::Slot slot) {
        if (confirm_ & bit(slot)) {
            confirm_ &= ~bit(slot);
            UI::get().send_key(GLFMKeyCodeEnter, false);
            return;
        }

        if (cancel_ & bit(slot)) {
            cancel_ &= ~bit(slot);
            UI::get().send_key(GLFMKeyCodeEscape, false);
            return;
        }

        const Keyboard::Mapping mapping = GamepadConfig::get().mapping(slot);

        if (mapping.type == KeyType::Id::NONE) {
            return;
        }

        if (mapping.type == KeyType::Id::ACTION) {
            const Direction direction = to_direction(mapping.action);

            if (direction != Direction::NONE) {
                release_direction(direction, slot);
                return;
            }
        }

        if (mapping.action == KeyAction::Id::ATTACK
            || mapping.action == KeyAction::Id::PICKUP
            || mapping.action == KeyAction::Id::INTERACT_HARVEST) {
            return;
        }

        UI::get().send_key(GamepadConfig::keycode(slot), false);
    }

    void Gamepad::press_direction(Direction direction, GamepadConfig::Slot slot) {
        DirectionState &held = directions_[direction];

        if (held.holders++ == 0) {
            held.slot = slot;
        }

        held.pressed = clock::now();
        UI::get().send_key(GamepadConfig::keycode(slot), true);
    }

    void Gamepad::release_direction(Direction direction, GamepadConfig::Slot slot) {
        DirectionState &held = directions_[direction];

        if (held.holders == 0) {
            return;
        }

        if (--held.holders == 0) {
            UI::get().send_key(GamepadConfig::keycode(held.slot), false);
            held.slot = GamepadConfig::Slot::LENGTH;
        }
    }

    void Gamepad::clear_directions() {
        for (Direction direction : kDirections) {
            DirectionState &held = directions_[direction];

            if (held.holders > 0) {
                UI::get().send_key(GamepadConfig::keycode(held.slot), false);
                held.holders = 0;
                held.slot = GamepadConfig::Slot::LENGTH;
            }
        }

        stick_dirs_[0] = NONE;
        stick_dirs_[1] = NONE;
    }

    void Gamepad::update() {
        if (!connected_) {
            return;
        }

        const auto now = clock::now();

        // A stick held to a constant reading reports nothing more, so silence while something is
        // still engaged isn't taken for the controller having gone away.
        if (now > connect_expiry_) {
            if (state_ == 0 && stick_dirs_[0] == NONE && stick_dirs_[1] == NONE) {
                disconnect();
                return;
            }

            connect_expiry_ = now + CONNECT_TIMEOUT;
        }

        if (UI::get().get_state_type() != typeid(UIStateGame)) {
            return;
        }

        // Held buttons keep their action going; Android doesn't report them again.
        const auto &mappings = GamepadConfig::get().mappings();

        for (uint8_t slot = 0; slot < GamepadConfig::Slot::LENGTH; ++slot) {
            const uint32_t held = 1u << slot;

            if (!(state_ & held)) {
                continue;
            }

            const Keyboard::Mapping &mapping = mappings[slot];

            if (mapping.action == KeyAction::Id::ATTACK
                && !(cancel_ & held) && now - attack_repeat_ >= ATTACK_REPEAT) {
                tap(KeyAction::Id::ATTACK);
                attack_repeat_ = now;
            }
            else if (mapping.action == KeyAction::Id::PICKUP
                     && now - pickup_repeat_ >= PICKUP_REPEAT) {
                Stage::get().pick_up_near(Constants::PICKUP_RANGE);
                pickup_repeat_ = now;
            }
            else if (mapping.action == KeyAction::Id::INTERACT_HARVEST
                     && now - talk_repeat_ >= TALK_REPEAT) {
                Stage::get().get_npcs().talk_to_nearest(
                        Stage::get().get_player().get_position(),
                        Constants::TALK_RANGE);
                talk_repeat_ = now;
            }
        }

        // Walking is re-asserted for the same reason attacking is: a held stick gives exactly one
        // press, and a press that arrives while the character can't take it - mid attack, before
        // the map has gone active - is then lost until the stick is centred and pushed again. A
        // keyboard arrow hides the same hole because Android repeats it. Only the two axes that
        // just change direction are renewed; up and down grab ladders and portals, which has to
        // stay a single press, and a window that owns the screen answers keys itself.
        if (!UI::get().is_modal_active()) {
            for (Direction direction : {LEFT, RIGHT}) {
                DirectionState &held = directions_[direction];

                if (held.holders > 0 && now - held.pressed >= WALK_REPEAT) {
                    UI::get().send_key(GamepadConfig::keycode(held.slot), true);
                    held.pressed = now;
                }
            }
        }
    }

    void Gamepad::tap(KeyAction::Id action) {
        Stage::get().send_key(KeyType::Id::ACTION, action, true);
        Stage::get().send_key(KeyType::Id::ACTION, action, false);
    }

    uint8_t Gamepad::steer(double axis_x, double axis_y, uint8_t held) {
        // The axis the stick is furthest along owns the direction and the other one is treated as
        // centered. That puts the boundary between two directions at 45 degrees instead of letting
        // a diagonal push send both keys, which the game reads as climbing a rope or lying down.
        const double magnitude_x = std::abs(axis_x);
        const double magnitude_y = std::abs(axis_y);
        const bool use_x =
                std::abs(magnitude_x - magnitude_y) < Constants::STICK_AXIS_MARGIN
                ? (held & (LEFT | RIGHT)) != 0
                : magnitude_x > magnitude_y;
        const uint8_t resolved =
            use_x ? (axis_x < 0 ? LEFT : RIGHT) : (axis_y < 0 ? UP : DOWN);

        // How far the stick has travelled from its centre, which is what the deadzone is measured
        // against. Judging the two axes separately would make a diagonal push have to travel forty
        // two percent to do what a straight one gets away with at the deadzone itself.
        const double travel = std::hypot(axis_x, axis_y);

        if (travel >= Constants::STICK_DEADZONE) {
            return resolved;
        }

        // Below the deadzone a direction is kept only while the stick still points at it. Keeping
        // any direction that was held would leave the one the stick just came from engaged all the
        // way back through the band, so the next direction would have to cross the deadzone from
        // wherever the stick happened to stop rather than from its centre - and a swing straight
        // across to the opposite side would take the new direction as soon as it passed the low
        // threshold, which is what makes the deadzone look like it only works one way.
        if (travel >= Constants::STICK_RETURN && resolved == held) {
            return held;
        }

        return NONE;
    }

    void Gamepad::apply_stick(int stick, double x, double y) {
        const double axis_x = std::clamp(x, -1.0, 1.0);
        const double axis_y = std::clamp(y, -1.0, 1.0);

        // A controller reports its resting stick once when it connects, which must not take the
        // on-screen controls away from a player using touch.
        if (!connected_ && std::hypot(axis_x, axis_y) <= Constants::STICK_DEADZONE) {
            return;
        }

        connected_ = true;
        connect_expiry_ = clock::now() + CONNECT_TIMEOUT;

        const uint8_t dirs = steer(axis_x, axis_y, stick_dirs_[stick]);

        if (dirs == stick_dirs_[stick]) {
            return;
        }

        const uint8_t changed = dirs ^ stick_dirs_[stick];

        // Releases go out before presses. Every state reacts only to a press, and a press landing
        // while the old direction still has the character prone or climbing is dropped, which would
        // leave the character standing still until the stick was centred and pushed again.
        for (Direction direction : kDirections) {
            if ((changed & direction) && !(dirs & direction)) {
                release(stick_slot(stick, direction));
            }
        }

        for (Direction direction : kDirections) {
            if (changed & direction && (dirs & direction)) {
                press(stick_slot(stick, direction));
            }
        }

        stick_dirs_[stick] = dirs;
    }

    void Gamepad::set_left_stick(double x, double y) {
        apply_stick(0, x, y);
    }

    void Gamepad::set_right_stick(double x, double y) {
        apply_stick(1, x, y);
    }

    void Gamepad::disconnect() {
        connected_ = false;
        state_ = 0;
        confirm_ = 0;
        cancel_ = 0;
        clear_directions();
    }
}  // namespace ms
