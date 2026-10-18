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

#include "KeyAction.h"
#include "Singleton.h"

namespace ms {
    // Controller support. Buttons arrive through the key callback because glfm's Android backend
    // was extended with the AKEYCODE_BUTTON_* mappings; glfm hands the left stick over separately,
    // through glfmJoystickAxisFunc.
    class Gamepad : public Singleton<Gamepad> {
    public:
        enum class Button : uint8_t {
            A,
            B,
            X,
            Y,
            LEFT_SHOULDER,
            RIGHT_SHOULDER,
            LEFT_TRIGGER,
            RIGHT_TRIGGER,
            SELECT,
            START,
            LEFT_STICK,
            RIGHT_STICK,
            MODE
        };

        Gamepad() = default;

        // Apply a button event. Returns false when the key code isn't a controller button, so the
        // caller can fall through to normal keyboard handling.
        bool on_key(int32_t keycode, bool pressed);

        // Feed a left-stick reading, in axis units (-1 to 1).
        void set_stick(double x, double y);

        // Repeat held actions and forget a controller that went quiet. Call once per frame.
        void update();

        bool is_connected() const;

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
        // Loot has to be right at the feet. Counted to the nearest edge of the item's picture rather
        // than to the point it is stored at, which is what made the reach differ by side.
        static constexpr int16_t PICKUP_RANGE = 24;
        static constexpr int16_t TALK_RANGE = 350;

        enum Direction : uint8_t { NONE = 0, LEFT = 1, RIGHT = 2, UP = 4, DOWN = 8 };

        using clock = std::chrono::steady_clock;

        static uint32_t bit(Button button);

        void press(Button button);

        void release(Button button);

        void tap(KeyAction::Id action);

        // Sends a key release for every direction the stick is currently holding.
        void clear_directions();

        void disconnect();

        bool connected_ = false;
        uint32_t state_ = 0;
        // Remember how a held A or B was interpreted, since a dialog can open underneath it.
        bool confirm_held_ = false;
        bool cancel_held_ = false;

        uint8_t stick_dirs_ = NONE;
        clock::time_point connect_expiry_ = clock::now();
        clock::time_point attack_repeat_ = clock::now();
        clock::time_point pickup_repeat_ = clock::now();
        clock::time_point talk_repeat_ = clock::now();
    };
}  // namespace ms
