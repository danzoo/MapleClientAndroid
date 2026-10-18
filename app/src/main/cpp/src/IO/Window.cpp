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
#include "Window.h"
#include "../Configuration.h"
#include "../Constants.h"
#include "../Graphics/GraphicsGL.h"
#include "../Timer.h"
#include "UI.h"
#include "stb/stb_image.h"
#include "GLES3/gl32.h"
#include "gl.h"
#include "Char.h"
#include "MapPortals.h"
#include "Singleton.h"
#include "Stage.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <android/log.h>

#include "Gamepad.h"

namespace ms {
    Window::Window() :
            display(nullptr),
            fullscreen_(true),
            opacity_(1.0f),
            opc_step_(0.0f) {
    }

    Window::~Window() {
        display = nullptr;
//        glfmTerminate();
    }

    void error_callback(int no, const char *description) {
        std::cout << "GLFW error [" << no << "]: " << description << std::endl;
    }

    bool key_callback(GLFMDisplay *display, GLFMKeyCode keyCode, GLFMKeyAction action,
                      int modifiers) {
        std::cout << "key_callback" << keyCode << " action " << action << std::endl;

        if (Gamepad::get().on_key(keyCode, action != GLFMKeyActionReleased)) {
            return true;
        }

        UI::get().send_key(keyCode, action != GLFMKeyActionReleased);
        return true;
    }

    void char_callback(GLFMDisplay *display, const char *string, int modifiers) {
        std::cout << "char_callback" << string << std::endl;
        // Determine the length of the wide string (Unicode) to allocate memory
        size_t length = mbstowcs(nullptr, string, 0);
        if (length == (size_t) -1) {
            // Error handling for invalid input string
            return;
        }

        // Allocate memory for the wide string
        wchar_t *wide_string = new wchar_t[length + 1]; // +1 for null terminator

        // Convert the input string to wide string (Unicode)
        mbstowcs(wide_string, string, length);
        wide_string[length] = L'\0'; // Null-terminate the wide string

        // Convert each wide character to uint32_t and use it as needed
        for (size_t i = 0; i < length; ++i) {
            uint32_t unicode_value = static_cast<uint32_t>(wide_string[i]);
            // Now you have the uint32_t representation of the Unicode character
            // You can use it as needed
            UI::get().send_key(unicode_value);
        }
        delete[] wide_string;
    }

    std::chrono::time_point<std::chrono::steady_clock> start =
            ContinuousTimer::get().start();

    std::chrono::time_point<std::chrono::steady_clock> click_start_time;

    bool sent = false;

    void joystick_axis_callback(GLFMDisplay *display, double x, double y) {
        Gamepad::get().set_stick(x, y);
    }

    bool mousekey_callback(GLFMDisplay *display, int touch, GLFMTouchPhase phase,
                           double x, double y) {
        Window::get().move_cursor(x, y);
        switch (phase) {
            case GLFMTouchPhaseHover:
                break;
            case GLFMTouchPhaseBegan: {
                click_start_time = std::chrono::steady_clock::now();
                UI::get().send_cursor(false);
                break;
            }
            case GLFMTouchPhaseMoved: {
                auto diff_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - click_start_time).count();
                if (diff_ms > 500) {
                    if (!sent)
                        UI::get().send_cursor(true);
                } else {
                    UI::get().send_cursor(false);
                }
                break;
            }
            case GLFMTouchPhaseEnded: {
                if (sent)
                    sent = false;
                else {
                    UI::get().send_cursor(true);
                }
                auto diff_ms = ContinuousTimer::get().stop(start) / 1000;
                start = ContinuousTimer::get().start();

                if (diff_ms > 10 && diff_ms < 200) {
                    UI::get().doubleclick();
                }
                UI::get().send_cursor(false);
                break;
            }
            case GLFMTouchPhaseCancelled:
                break;
        }

        return true;
    }

    void Window::move_cursor(double x, double y) {
        Point<double_t> game_pos = to_game(x, y);
        auto pos = Point<int16_t>(static_cast<int16_t>(game_pos.x()),
                                  static_cast<int16_t>(game_pos.y()));
        UI::get().send_cursor(pos);
    }

    void focus_callback(GLFMDisplay *display, bool focused) {
        UI::get().send_focus(focused);
    }

    void scroll_callback(GLFMDisplay *display, double xoffset, double yoffset) {
        UI::get().send_scroll(yoffset);
    }

    void close_callback(GLFMDisplay *display) {
        UI::get().send_close();
    }

    void onDraw(GLFMDisplay *display) {
        if (ms::program_ == 0) {
            program_ = glCreateProgram();
            GraphicsGL::get().init(display);
            GraphicsGL::get().reinit();
            Char::init();
            DamageNumber::init();
            MapPortals::init();
            Stage::get().init();
            UI::get().init();
            Timer::get().start();
        }
        const int64_t timestep = Constants::TIMESTEP * 1000;
        int64_t accumulator = timestep;
        if (game.is_running()) {
            Gamepad::get().update();
            int64_t elapsed = Timer::get().stop();

            // Update game with constant timestep as many times as possible.
            for (accumulator += static_cast<int64_t>(elapsed * Constants::SIMULATION_SPEED);
                 accumulator >= timestep; accumulator -= timestep) {
                game.update();
            }
            float alpha = static_cast<float>(accumulator) / timestep;
            game.draw(alpha);
        } else {
            // Nothing is drawn until the server connection is up, which would leave the surface
            // presenting the uninitialised contents it was created with.
            int surface_width, surface_height;
            glfmGetDisplaySize(display, &surface_width, &surface_height);

            glViewport(0, 0, surface_width, surface_height);
            glDisable(GL_SCISSOR_TEST);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glfmSwapBuffers(display);
        }
    }

    Error Window::init(GLFMDisplay *pApp) {
        display = pApp;
        glfmSetDisplayConfig(display,
                             GLFMRenderingAPIOpenGLES32,
                             GLFMColorFormatRGBA8888,
                             GLFMDepthFormatNone,
                             GLFMStencilFormatNone,
                             GLFMMultisampleNone);
        glfmSetMultitouchEnabled(display, true);
        return init_window();
    }

    Error Window::init_window() {
        glfmSetMouseCursor(display, GLFMMouseCursorDefault);
        glfmSetKeyFunc(display, key_callback);
        glfmSetCharFunc(display, char_callback);
        glfmSetRenderFunc(display, onDraw);
        glfmSetTouchFunc(display, mousekey_callback);
        glfmJoystickAxisFunc = joystick_axis_callback;
//        glfmSetMouseWheelFunc(display, cursor_callback);
        glfmSetAppFocusFunc(display, focus_callback);
//        glfwSetScrollCallback(pApp, scroll_callback);
        glfmSetSurfaceDestroyedFunc(display, close_callback);

        return Error::Code::NONE;
    }

    bool Window::not_closed() const {
//        return glfwWindowShouldClose(glwnd_) == 0;
        return true;
    }

    void Window::update() {
        update_opc();
    }

    void Window::update_opc() {
        if (opc_step_ != 0.0f) {
            opacity_ += opc_step_;

            if (opacity_ >= 1.0f) {
                opacity_ = 1.0f;
                opc_step_ = 0.0f;
            } else if (opacity_ <= 0.0f) {
                opacity_ = 0.0f;
                opc_step_ = -opc_step_;

                fade_procedure_();
            }
        }
    }

    void Window::check_events() {
        int16_t max_width = Configuration::get().get_max_width();
        int16_t max_height = Configuration::get().get_max_height();
        int16_t view_width = Constants::Constants::get().get_viewwidth();
        int16_t view_height = Constants::Constants::get().get_viewheight();

        if (view_width >= max_width || view_height >= max_height) {
            fullscreen_ = true;
        }
    }

    void Window::begin() {
        GraphicsGL::get().sync_screen_size();
        update_viewport();
        GraphicsGL::get().clear_scene();
    }

    void Window::end() const {
        GraphicsGL::get().flush(opacity_);
        glfmSwapBuffers(display);
    }

    void Window::fadeout(float step, std::function<void()> fadeproc) {
        opc_step_ = -step;
        fade_procedure_ = std::move(fadeproc);
    }

    void Window::set_clipboard(const std::string &text) const {
//        glfwSetClipboardString(glwnd_, text.c_str());
    }

    std::string Window::get_clipboard() const {
//        const char *text = glfwGetClipboardString(glwnd_);

//        return text != nullptr ? text : "";
        return "";
    }

    void Window::toggle_fullscreen() {
        int16_t max_width = Configuration::get().get_max_width();
        int16_t max_height = Configuration::get().get_max_height();
        int16_t view_width = Constants::Constants::get().get_viewwidth();
        int16_t view_height = Constants::Constants::get().get_viewheight();

        if (view_width < max_width && view_height < max_height) {
            fullscreen_ = !fullscreen_;
            Setting<Fullscreen>::get().save(fullscreen_);
        }
    }

    void Window::update_viewport() {
        int surface_width, surface_height;
        glfmGetDisplaySize(display, &surface_width, &surface_height);

        if (surface_width <= 0 || surface_height <= 0) {
            return;
        }

        // Fit the size the scene is actually being drawn at, which the renderer holds back while a
        // transition fades: a box cut for a resolution the picture isn't drawn at yet leaves that
        // picture in the corner of it.
        const int view_width = GraphicsGL::get().get_screen_width();
        const int view_height = GraphicsGL::get().get_screen_height();

        // Fitting the view by the smaller scale keeps the whole picture and its aspect; anything
        // the surface has to spare on that axis becomes a bar.
        const double scale
                = std::min(static_cast<double>(surface_width) / view_width,
                           static_cast<double>(surface_height) / view_height);
        const int32_t box_width = static_cast<int32_t>(std::lround(view_width * scale));
        const int32_t box_height = static_cast<int32_t>(std::lround(view_height * scale));

        scale_ = scale;
        offset_x_ = (surface_width - box_width) / 2;
        offset_y_ = (surface_height - box_height) / 2;

        glViewport(offset_x_, offset_y_, box_width, box_height);
        // Keeps the bars black, since the view itself is cleared over its own box.
        glScissor(offset_x_, offset_y_, box_width, box_height);
    }

    Point<double_t> Window::to_game(double x, double y) const {
        // Touches arrive as surface pixels from the top left. The view box is centred, so the bar
        // trimmed off the top is the same height as the one at the bottom.
        return Point<double_t>((x - offset_x_) / scale_, (y - offset_y_) / scale_);
    }

    GLFMDisplay *Window::get_display() {
        return display;
    }
}  // namespace ms