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

#include "../Error.h"
#include "../Template/Point.h"
#include "../Template/Singleton.h"
#include "glfm.h"
#include <functional>
#include <string>
#include <unistd.h>
#include <iostream>
#include <unistd.h>
#define GetCurrentDir getcwd

namespace ms {
    class Window : public Singleton<Window> {
    public:
        Window();

        ~Window() override;

        Error init(GLFMDisplay *pApp);

        Error init_window();

        void update();

        void begin();

        void end() const;

        // Fade the whole screen out over the given number of milliseconds, then run the procedure.
        void fadeout(uint16_t millis, std::function<void()> fadeprocedure);

        void set_clipboard(const std::string &text) const;

        std::string get_clipboard() const;

        // Ends the application for good. glfm owns the render loop on its own thread, so unlike
        // the desktop build there is no loop condition to return from: the activity is asked to
        // remove its task and the process is terminated here.
        void terminate();

        void move_cursor(double x, double y);

        // Whether the press being delivered right now came out of a committed hold or drag rather
        // than out of a tap. Only this layer knows the difference — a tap sends its press and its
        // lift inside one event, a hold sends the press while the finger is still down — and a
        // widget that does both with one icon, like a quick-slot button that triggers on a tap and
        // picks up on a hold, cannot tell them apart on its own.
        bool is_hold_press() const;

        // Translate a touch position in surface pixels from the top left into the space the
        // interface is laid out in, undoing the letterbox offset and the interface's own scale.
        Point<double_t> to_ui(double x, double y) const;

        GLFMDisplay* get_display();

    private:
        void update_opc();

        // Stretch the game view over the surface, keeping its aspect and centring it.
        void update_viewport();

        GLFMDisplay *display;
        float opacity_;
        float opc_step_;
        // Surface pixels per game unit, and the centring offsets of the view box.
        double scale_ = 1.0;
        int32_t offset_x_ = 0;
        int32_t offset_y_ = 0;
        // The view box and the surface around it, which the frame has to be stretched onto.
        int32_t box_width_ = 0;
        int32_t box_height_ = 0;
        int32_t surface_width_ = 0;
        int32_t surface_height_ = 0;
        std::function<void()> fade_procedure_;
    };
}  // namespace ms