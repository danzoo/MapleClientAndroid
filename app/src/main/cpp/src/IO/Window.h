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

        bool not_closed() const;

        void update();

        void begin();

        void end() const;

        void fadeout(float step, std::function<void()> fadeprocedure);

        void check_events();

        void set_clipboard(const std::string &text) const;

        std::string get_clipboard() const;

        void toggle_fullscreen();

        void move_cursor(double x, double y);

        // Translate a touch position in surface pixels from the top left into the space the
        // interface is laid out in, undoing the letterbox offset and the interface's own scale.
        Point<double_t> to_ui(double x, double y) const;

        GLFMDisplay* get_display();

    private:
        void update_opc();

        // Stretch the game view over the surface, keeping its aspect and centring it.
        void update_viewport();

        GLFMDisplay *display;
        bool fullscreen_;
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