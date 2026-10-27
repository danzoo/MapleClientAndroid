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
#include "../Audio/Audio.h"
#include "../Graphics/GraphicsGL.h"
#include "../Graphics/UIRenderer.h"
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
#include <chrono>
#include <cmath>
#include <utility>
#include <EGL/egl.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <jni.h>

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

        // The device's volume keys are the keys with no mapping of their own, and claiming to have
        // handled one is what stops the framework from showing its own volume slider.
        if (keyCode == GLFMKeyCodeUnknown) {
            return false;
        }

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

    // Touch is the only pointer this device has. A single finger swiping vertically scrolls the
    // window under it, row by row; a finger put down on a window's title strip drags that window
    // right away; holding still for ~500 ms picks up an icon or grabs the slider thumb, exactly as
    // before. Scroll-vs-hold is decided by whichever threshold the finger crosses first.
    int active_touches = 0;
    int touch_mode = 0;  // 0 undecided, 1 hold-grab/click, 2 scroll, 3 window drag, 4 icon already grabbed
    double touch_start_x = 0.0;
    double touch_start_y = 0.0;
    double scroll_last_y = 0.0;
    double scroll_acc = 0.0;

    // Surface pixels one finger travels per row scrolled. Rows are the smallest unit there is,
    // so a swipe never leaves a list between two rows.
    constexpr double SCROLL_ROW_PX = 60.0;

    // Surface pixels of vertical travel that commit an undecided finger to scrolling instead of
    // waiting out the hold-to-drag timeout.
    constexpr double SCROLL_SLOP_PX = 12.0;

    void joystick_axis_callback(GLFMDisplay *display, double x, double y) {
        Gamepad::get().set_stick(x, y);
    }

    bool mousekey_callback(GLFMDisplay *display, int touch, GLFMTouchPhase phase,
                           double x, double y) {
        // While a scroll gesture owns the finger the cursor is frozen where scrolling began.
        // send_scroll routes to the window under the cursor, so freezing it keeps that same
        // window scrolling no matter where the finger drifts, and skipping the hover pass stops
        // new item previews from popping up as the finger travels over them.
        if (touch_mode != 2) {
            Window::get().move_cursor(x, y);
        }
        switch (phase) {
            case GLFMTouchPhaseHover:
                break;
            case GLFMTouchPhaseBegan: {
                click_start_time = std::chrono::steady_clock::now();
                active_touches++;

                if (active_touches == 1) {
                    touch_start_x = x;
                    touch_start_y = y;
                    scroll_last_y = y;
                    scroll_acc = 0.0;

                    if (UI::get().get_cursor_state() == Cursor::State::GRABBING) {
                        // An icon is already picked up from a previous gesture. This finger
                        // drags it around; a single stationary tap on release drops it. Do not
                        // press here — a press would drop the icon immediately.
                        touch_mode = 4;
                    } else if (UI::get().cursor_in_drag_area()) {
                        touch_mode = 3;  // title strip: drag the window immediately
                        UI::get().send_cursor(true);
                    } else {
                        touch_mode = 0;  // wait to see if this is a tap, a swipe or a hold
                        UI::get().send_cursor(false);
                    }
                } else {
                    // Scrolling is single-finger only; a second finger cancels the gesture.
                    touch_mode = 0;
                    UI::get().send_cursor(false);
                }
                break;
            }
            case GLFMTouchPhaseMoved: {
                if (active_touches != 1) {
                    break;
                }

                if (touch_mode == 3) {
                    UI::get().send_cursor(true);
                    break;
                }

                if (touch_mode == 4) {
                    // Dragging an already-grabbed icon. move_cursor at the top of this event
                    // re-sends the persisted GRABBING state, so the icon follows the finger.
                    break;
                }

                if (touch_mode == 1) {
                    // The grab was already sent once on the hold transition below. move_cursor at
                    // the top of this event keeps the drag alive through the persisted cursor
                    // state (GRABBING for an icon, VSCROLL_IDLE for the slider thumb), so sending
                    // another press here would re-click and drop-then-regrab every frame.
                    break;
                }

                if (touch_mode == 2) {
                    scroll_acc += y - scroll_last_y;
                    scroll_last_y = y;

                    while (scroll_acc >= SCROLL_ROW_PX) {
                        scroll_acc -= SCROLL_ROW_PX;
                        UI::get().send_scroll(1.0);
                    }

                    while (scroll_acc <= -SCROLL_ROW_PX) {
                        scroll_acc += SCROLL_ROW_PX;
                        UI::get().send_scroll(-1.0);
                    }
                    break;
                }

                // touch_mode == 0: still undecided between a tap, a swipe and a hold.
                double dx = x - touch_start_x;
                double dy = y - touch_start_y;
                double adx = dx < 0.0 ? -dx : dx;
                double ady = dy < 0.0 ? -dy : dy;

                if (ady > SCROLL_SLOP_PX && ady > adx) {
                    touch_mode = 2;  // a vertical swipe scrolls
                    scroll_last_y = y;
                    scroll_acc = 0.0;
                    // Committing to a scroll dismisses any item preview the hover pass raised on
                    // the way here; the cursor is frozen from now on, so none reappears.
                    UI::get().clear_tooltip();
                    break;
                }

                auto diff_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - click_start_time).count();
                if (diff_ms > 500) {
                    touch_mode = 1;  // held still: pick up the icon / grab the slider thumb once
                    UI::get().send_cursor(true);
                    break;
                }

                UI::get().send_cursor(false);  // still hovering: keeps the item tooltip alive
                break;
            }
            case GLFMTouchPhaseEnded:
            case GLFMTouchPhaseCancelled: {
                if (active_touches > 0) {
                    active_touches--;
                }

                if (touch_mode == 2) {
                    touch_mode = 0;
                    break;  // a finger that was scrolling never taps on the way up
                }

                if (touch_mode == 3) {
                    UI::get().send_cursor(false);
                    touch_mode = 0;
                    break;  // the drag already moved the window; no tap on top of it
                }

                bool cancelled = phase == GLFMTouchPhaseCancelled;

                auto down_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - click_start_time).count();
                double adx = std::fabs(x - touch_start_x);
                double ady = std::fabs(y - touch_start_y);
                bool stationary = (adx > ady ? adx : ady) <= SCROLL_SLOP_PX;

                if (cancelled) {
                    touch_mode = 0;
                    break;  // a cancelled finger neither taps nor drops anything
                }

                if (touch_mode == 4) {
                    // This finger went down on an already-grabbed icon.
                    touch_mode = 0;
                    if (stationary) {
                        // A single tap while holding: drop the icon here.
                        UI::get().send_cursor(true);
                        UI::get().send_cursor(false);
                    }
                    // Otherwise it was a drag: the icon stays grabbed and follows the next finger.
                    break;
                }

                if (touch_mode == 1) {
                    // Grabbed during this gesture by holding past the timeout.
                    touch_mode = 0;
                    if (UI::get().get_cursor_state() == Cursor::State::GRABBING) {
                        if (stationary) {
                            // Held the item without dragging it: keep it grabbed (sticky). A
                            // later single tap (touch_mode 4) drops it. Leaving the state
                            // GRABBING is what makes the next Began see it as already picked up.
                            break;
                        }
                        // Held AND dragged to a new spot: drop it here — the classic
                        // grab-drag-release. send_cursor(true) toggles UIStateGame's
                        // if(dragged_icon_) drop branch at the release position.
                        UI::get().send_cursor(true);
                        UI::get().send_cursor(false);
                        break;
                    }
                    // A slider thumb or a plain held button: release it as before.
                    UI::get().send_cursor(true);
                } else {
                    // touch_mode == 0: never committed to a scroll, a window drag or a hold-move.
                    touch_mode = 0;

                    if (stationary && down_ms > 500
                        && UI::get().get_cursor_state() == Cursor::State::CAN_GRAB) {
                        // A long press that never moved (no Moved events to trip the timeout
                        // above): pick the item up now and keep holding it, same sticky grab.
                        UI::get().send_cursor(true);
                        break;
                    }

                    // A quick tap. Over an item it must only preview it — the tooltip is already
                    // up from the hover phase — so skip the press that would grab the icon. Over
                    // anything else (a button, say) the tap is a normal click.
                    Cursor::State st = UI::get().get_cursor_state();
                    bool over_item = st == Cursor::State::CAN_GRAB
                                     || st == Cursor::State::GRABBING;

                    if (!over_item) {
                        if (sent)
                            sent = false;
                        else
                            UI::get().send_cursor(true);
                    }
                }

                auto diff_ms = ContinuousTimer::get().stop(start) / 1000;
                start = ContinuousTimer::get().start();

                if (diff_ms > 10 && diff_ms < 200) {
                    UI::get().doubleclick();
                }
                UI::get().send_cursor(false);
                break;
            }
        }

        return true;
    }

    void Window::move_cursor(double x, double y) {
        Point<double_t> ui_pos = to_ui(x, y);
        auto pos = Point<int16_t>(static_cast<int16_t>(ui_pos.x()),
                                  static_cast<int16_t>(ui_pos.y()));
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

    // The rate the frames actually reach the panel, measured over a window long enough that the
    // rounding of one frame cannot show. The cap itself is not here: it is the render loop's own
    // deadline, which waits before calling this function rather than letting the frame through and
    // refusing it afterwards, so every callback here draws and every drawing is swapped.
    void count_frames() {
        using clock = std::chrono::steady_clock;
        constexpr uint16_t WINDOW = 120;

        static clock::time_point window;
        static uint16_t frames = 0;

        if (!Configuration::get().get_show_fps()) {
            return;
        }

        const clock::time_point now = clock::now();
        if (frames++ == 0) {
            window = now;
        }

        if (frames == WINDOW) {
            // The window is timed between the first frame in it and the last, so it holds one
            // interval fewer than the count.
            const double seconds = std::chrono::duration<double>(now - window).count();
            __android_log_print(ANDROID_LOG_INFO, "Window", "present %.1f fps",
                                 (frames - 1) / seconds);
            frames = 0;
        }
    }

    void onDraw(GLFMDisplay *display) {
        if (ms::program_ == 0) {
            program_ = glCreateProgram();
            GraphicsGL::get().init(display);
            GraphicsGL::get().reinit();
            // One buffer swap per vsync, so a frame cannot be thrown at the panel faster than the
            // panel takes them and the queue never runs ahead of the picture.
            eglSwapInterval(eglGetCurrentDisplay(), 1);
            // The cap itself, held at the source: glfm's loop waits until the next frame is due before
            // calling this function, so onDraw runs at FRAME_RATE on a panel of any rate rather than
            // being entered more often and refused afterwards. Naming the rate to the compositor as
            // well so that a panel faster than the cap serves frames at the cap rather than at its own
            // rate, which is what leaves a frame the whole 1/60 to work in.
            glfmSetFrameRate(display, static_cast<float>(Constants::FRAME_RATE));
            __android_log_print(ANDROID_LOG_INFO, "Window", "panel %.0f Hz, capped at %.0f fps",
                                glfmGetDisplayRefreshRate(display), Constants::FRAME_RATE);
            Char::init();
            DamageNumber::init();
            MapPortals::init();
            Stage::get().init();
            UI::get().init();
        }

        count_frames();

        if (game.is_running()) {
            Gamepad::get().update();
            // The whole loop body, paced by the clock inside it: this frame is worth however many
            // updates the time since the last one buys, and the picture is drawn at the moment
            // between them that is left over. Held to FRAME_RATE by the deadline above, so the two
            // rates are set independently of each other.
            game.frame();
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
        GraphicsGL::get().begin_scene();
        GraphicsGL::get().clear_scene();
    }

    void Window::end() const {
        GraphicsGL::get().flush();
        GraphicsGL::get().end_scene(offset_x_, offset_y_, box_width_, box_height_,
                                    surface_width_, surface_height_, opacity_);
        glfmSwapBuffers(display);
    }

    void Window::fadeout(uint16_t millis, std::function<void()> fadeproc) {
        // The fade is spread over exactly the time it is given, so an update takes off the share of
        // the opacity that its own length is worth of it.
        opc_step_
            = -static_cast<float>(Constants::TIMESTEP) / static_cast<float>(millis);
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
        box_width_ = box_width;
        box_height_ = box_height;
        surface_width_ = surface_width;
        surface_height_ = surface_height;

        // The interface is rasterised straight into a target this size, so it comes out at native
        // pixel density instead of being drawn small and stretched over the box afterwards.
        UIRenderer::get().set_native_size(box_width, box_height);

        // Both passes magnify by a ratio of this box to their own screen, and text has to be
        // rasterised at that ratio to reach the screen one texel per pixel.
        GraphicsGL::get().set_output_size(box_width, box_height);

        glViewport(offset_x_, offset_y_, box_width, box_height);
        // Keeps the bars black, since the view itself is cleared over its own box.
        glScissor(offset_x_, offset_y_, box_width, box_height);
    }

    Point<double_t> Window::to_ui(double x, double y) const {
        // Touches arrive as surface pixels from the top left. The view box is centred, so the bar
        // trimmed off the top is the same height as the one at the bottom.
        const double_t view_x = (x - offset_x_) / scale_;
        const double_t view_y = (y - offset_y_) / scale_;

        // The cursor belongs to the interface, which is drawn on a screen UI_SCALE smaller than
        // the view, so that is the space a touch has to be read in.
        return Point<double_t>(view_x / Constants::UI_SCALE, view_y / Constants::UI_SCALE);
    }

    GLFMDisplay *Window::get_display() {
        return display;
    }

    void Window::terminate() {
        // BASS runs its own threads and holds the audio device, so releasing it first is what stops
        // the music being cut off mid-buffer and leaves the device free for whatever starts next.
        Sound::close();

        auto *activity = static_cast<ANativeActivity *>(glfmGetAndroidActivity(display));

        if (activity != nullptr && activity->vm != nullptr) {
            // ANativeActivity_finish only queues a message on the Java main thread, which this
            // process is about to stop having, so the request to close the window has to be made
            // synchronously here. finishAndRemoveTask is what takes the task out of recents rather
            // than leaving a dead entry behind.
            JNIEnv *env = nullptr;

            if (activity->vm->AttachCurrentThread(&env, nullptr) == JNI_OK && env != nullptr) {
                const jclass clazz = env->GetObjectClass(activity->clazz);
                const jmethodID finish = env->GetMethodID(clazz, "finishAndRemoveTask", "()V");

                if (finish != nullptr) {
                    env->CallVoidMethod(activity->clazz, finish);
                }

                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                }

                env->DeleteLocalRef(clazz);
            }
        }

        // glfm's frame loop is what this callback was called from, so there is no way back out of
        // it, and running the static destructors would race with the network thread that is still
        // alive. Ending the process directly is the clean exit; the socket goes with it, which is
        // how the server learns the client is gone.
        _exit(0);
    }
}  // namespace ms