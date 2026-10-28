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

#include <chrono>
#include <cstdint>

#include "GamepadConfig.h"
#include "KeyAction.h"
#include "Singleton.h"

namespace ms {
    // Controller support. Buttons and the d-pad arrive through the key callback because glfm's
    // Android backend was extended with the AKEYCODE_BUTTON_* mappings and with d-pad codes of
    // their own; the two thumbsticks arrive separately, through glfmJoystickAxisFunc and
    // glfmJoystickAxis2Func.
    //
    // What each place on the controller does is the player's business - it lives in GamepadConfig -
    // so this class is only about turning a raw event into the press of a binding, and about the
    // handful of behaviours that belong to a controller rather than to a key.
    class Gamepad : public Singleton<Gamepad> {
    public:
        // The four ways the game can move, as bits so a stick can hold more than one at once.
        enum Direction : uint8_t { NONE = 0, LEFT = 1, RIGHT = 2, UP = 4, DOWN = 8 };

        Gamepad() = default;

        // Apply a button event. Returns false when the key code isn't a controller input, so the
        // caller can fall through to normal keyboard handling.
        bool on_key(int32_t keycode, bool pressed);

        // Feed a thumbstick reading, in axis units (-1 to 1).
        void set_left_stick(double x, double y);

        void set_right_stick(double x, double y);

        // Repeat held actions and forget a controller that went quiet. Call once per frame.
        void update();

    private:
        static constexpr double STICK_DEADZONE = 0.3;
        // Directions stay engaged until the stick returns inside this threshold, so that holding
        // it near the edge of the deadzone doesn't flicker between walking and standing.
        static constexpr double STICK_RETURN = 0.15;
        // Around the diagonal the axis that already owns the direction keeps owning it, so a stick
        // parked near 45 degrees doesn't flicker between the two keys as the axes jitter.
        static constexpr double STICK_AXIS_MARGIN = 0.06;
        // No disconnect event reaches the app, so a controller is forgotten once it stays quiet.
        // A stick or button held at a constant reading reports nothing either, so that silence is
        // not treated as the controller going away while it is still being used.
        static constexpr std::chrono::milliseconds CONNECT_TIMEOUT{5000};
        static constexpr std::chrono::milliseconds ATTACK_REPEAT{250};
        static constexpr std::chrono::milliseconds PICKUP_REPEAT{100};
        static constexpr std::chrono::milliseconds TALK_REPEAT{500};

        using clock = std::chrono::steady_clock;

        // One physical controller input being engaged or let go. Everything a controller can do
        // starts here, whichever callback it arrived through.
        void press(GamepadConfig::Slot slot);

        void release(GamepadConfig::Slot slot);

        // The four directions are the one thing several inputs can ask for at once - the d-pad and
        // both sticks can all be bound to walking left - so a direction is only released when the
        // last of them lets go.
        void press_direction(Direction direction, GamepadConfig::Slot slot);

        void release_direction(Direction direction, GamepadConfig::Slot slot);

        void clear_directions();

        // Resolve one stick's axes into a direction and send the edges between the old and the new
        // one. The game moves in four directions only, so the boundary between two of them sits at
        // 45 degrees rather than letting a diagonal push send both keys.
        uint8_t steer(double x, double y, uint8_t held);

        void apply_stick(int stick, double x, double y);

        void tap(KeyAction::Id action);

        void disconnect();

        static Direction to_direction(int32_t action);

        static GamepadConfig::Slot stick_slot(int stick, Direction direction);

        static uint32_t bit(GamepadConfig::Slot slot);

        bool connected_ = false;
        // Every key-driven slot the controller reports as held.
        uint32_t state_ = 0;
        // Slots pressed while a dialog or menu owned the screen. They are let go as confirm and
        // cancel rather than as whatever they are bound to, because that is what they were doing.
        uint32_t confirm_ = 0;
        uint32_t cancel_ = 0;

        struct DirectionState {
            uint8_t holders = 0;
            // The slot that first asked for the direction, and so the one whose binding the release
            // has to undo.
            GamepadConfig::Slot slot = GamepadConfig::Slot::LENGTH;
        };

        // Indexed by the Direction bit, which runs from 1 to 8.
        DirectionState directions_[9] = {};
        uint8_t stick_dirs_[2] = {NONE, NONE};

        clock::time_point connect_expiry_ = clock::now();
        clock::time_point attack_repeat_ = clock::now();
        clock::time_point pickup_repeat_ = clock::now();
        clock::time_point talk_repeat_ = clock::now();
    };
}  // namespace ms
