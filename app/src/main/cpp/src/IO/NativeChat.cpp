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
#include "NativeChat.h"

#include <jni.h>

#include <ctime>

#include "../Constants.h"
#include "../Template/EnumMap.h"
#include "../../Net/Packets/MessagingPackets.h"
#include "Gamepad.h"
#include "Window.h"
#include "glfm.h"

namespace ms {
    namespace {
        // Measured against the update tick rather than the wall clock: the cooldown is walked down
        // by Constants::TIMESTEP once per update, so this is real milliseconds as the loop keeps
        // them.
        //
        // Held long enough to repeat, a controller asks for the bound skill again every so often,
        // and each ask that the character can't grant drops the same red line - a skill short on mp
        // complained once every three ticks. The line says nothing new between one ask and the next,
        // so a message of a given kind is only allowed to appear this often. Taps still report: the
        // counter is cold the first time and the message that a single press earns is never
        // swallowed.
        constexpr time_t MESSAGE_COOLDOWN_ = 3'000;

        JavaVM *g_vm = nullptr;
        jclass g_class = nullptr;
        jmethodID g_show = nullptr;
        jmethodID g_hide = nullptr;
        jmethodID g_append = nullptr;
        jmethodID g_pending = nullptr;
        jmethodID g_take = nullptr;
        jmethodID g_set_floating = nullptr;
        jmethodID g_set_faded = nullptr;
        jmethodID g_touched = nullptr;
        bool g_ready = false;
        bool g_wanted = false;
        bool g_shown = false;
        // Where the window stands relative to the interface, as the game last left it. The window
        // itself is the one that fades and gives its touches back; this is only kept so that a
        // controller held down does not ask for the same thing every frame.
        bool g_floating = true;
        // Whether the window was last asked to be at full strength. The screen's fade is stepped here
        // once at each end of it rather than every frame, because the window is a thing android
        // composites above the picture the fade works on and so cannot be carried along by it.
        bool g_faded = true;
        EnumMap<Messages::Type, time_t> g_cooldowns;

        JNIEnv *current_env() {
            if (!g_vm) {
                return nullptr;
            }

            JNIEnv *env = nullptr;

            if (g_vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) == JNI_OK) {
                return env;
            }

            if (g_vm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
                return env;
            }

            return nullptr;
        }

        // FindClass on a thread the VM never started cannot see app classes, so the window class is
        // reached through the activity's own loader - the same route the glyph rasterizer takes.
        jclass load_window_class(JNIEnv *env, ANativeActivity *activity) {
            jclass activity_class = env->GetObjectClass(activity->clazz);

            if (!activity_class) {
                return nullptr;
            }

            jmethodID get_class_loader =
                    env->GetMethodID(activity_class, "getClassLoader", "()Ljava/lang/ClassLoader;");
            env->DeleteLocalRef(activity_class);

            if (!get_class_loader) {
                env->ExceptionClear();
                return nullptr;
            }

            jobject class_loader = env->CallObjectMethod(activity->clazz, get_class_loader);

            if (!class_loader) {
                env->ExceptionClear();
                return nullptr;
            }

            jclass loader_class = env->GetObjectClass(class_loader);
            jmethodID load_class = env->GetMethodID(loader_class, "loadClass",
                                                    "(Ljava/lang/String;)Ljava/lang/Class;");
            env->DeleteLocalRef(loader_class);

            if (!load_class) {
                env->ExceptionClear();
                env->DeleteLocalRef(class_loader);
                return nullptr;
            }

            jstring name = env->NewStringUTF("openMapleClient.NativeChat");
            auto window = static_cast<jclass>(
                    env->CallObjectMethod(class_loader, load_class, name));

            env->DeleteLocalRef(name);
            env->DeleteLocalRef(class_loader);

            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                return nullptr;
            }

            return window;
        }

        bool resolve(ANativeActivity *activity) {
            if (g_class && g_show && g_hide && g_append && g_pending && g_take
                && g_set_floating && g_set_faded && g_touched) {
                return true;
            }

            if (!activity || !activity->vm) {
                return false;
            }

            g_vm = activity->vm;
            JNIEnv *env = current_env();

            if (!env) {
                return false;
            }

            jclass local = load_window_class(env, activity);

            if (!local) {
                return false;
            }

            g_class = static_cast<jclass>(env->NewGlobalRef(local));
            env->DeleteLocalRef(local);

            g_show = env->GetStaticMethodID(g_class, "show",
                                            "(Ljava/lang/Object;FIIIIIII)V");
            g_hide = env->GetStaticMethodID(g_class, "hide", "()V");
            g_append = env->GetStaticMethodID(g_class, "append",
                                              "(IILjava/lang/String;)V");
            g_pending = env->GetStaticMethodID(g_class, "pending", "()I");
            g_take = env->GetStaticMethodID(g_class, "take", "()Ljava/lang/String;");
            g_set_floating = env->GetStaticMethodID(g_class, "setFloating", "(Z)V");
            g_set_faded = env->GetStaticMethodID(g_class, "setFaded", "(Z)V");
            g_touched = env->GetStaticMethodID(g_class, "touched", "(II)Z");

            if (env->ExceptionCheck() || !g_show || !g_hide || !g_append
                || !g_pending || !g_take || !g_set_floating || !g_set_faded
                || !g_touched) {
                env->ExceptionClear();
                env->DeleteGlobalRef(g_class);
                g_class = nullptr;
                g_show = nullptr;
                g_hide = nullptr;
                g_append = nullptr;
                g_pending = nullptr;
                g_take = nullptr;
                g_set_floating = nullptr;
                g_set_faded = nullptr;
                g_touched = nullptr;

                return false;
            }

            return true;
        }

        // The table holds components as floats and no alpha; the window wants one packed colour, and
        // opaque is the only alpha an interface colour has ever had.
        int32_t packed(Color::Name name) {
            const GLfloat *rgb = Color::colors[name];

            auto red = static_cast<int32_t>(rgb[0] * 255.0f + 0.5f);
            auto green = static_cast<int32_t>(rgb[1] * 255.0f + 0.5f);
            auto blue = static_cast<int32_t>(rgb[2] * 255.0f + 0.5f);

            return static_cast<int32_t>(0xFF000000) | (red << 16) | (green << 8) | blue;
        }

        int32_t to_pixels(float units, float px_per_unit) {
            return static_cast<int32_t>(units * px_per_unit + 0.5f);
        }

        Color::Name colored(NativeChat::LineType type) {
            switch (type) {
                case NativeChat::LineType::RED: return Color::Name::DARKRED;
                case NativeChat::LineType::BLUE: return Color::Name::MEDIUMBLUE;
                case NativeChat::LineType::YELLOW: return Color::Name::YELLOW;
                case NativeChat::LineType::GREEN: return Color::Name::GREEN;
                default: return Color::Name::WHITE;
            }
        }

        // Lines typed into the window, taken in the order they were typed and put on the wire from
        // here - which is the thread that owns the connection, not the one that answered the
        // keyboard.
        void send_typed() {
            JNIEnv *env = current_env();

            if (!env) {
                return;
            }

            auto waiting = static_cast<int32_t>(
                    env->CallStaticIntMethod(g_class, g_pending));

            for (int32_t i = 0; i < waiting; i++) {
                auto line = static_cast<jstring>(
                        env->CallStaticObjectMethod(g_class, g_take));

                if (env->ExceptionCheck() || !line) {
                    env->ExceptionClear();
                    return;
                }

                const char *text = env->GetStringUTFChars(line, nullptr);

                if (text) {
                    if (*text) {
                        GeneralChatPacket(text, true).dispatch();
                    }

                    env->ReleaseStringUTFChars(line, text);
                }

                env->DeleteLocalRef(line);
            }
        }

        // Hands the window's standing to the window, once per change. Every trigger for it is a thing
        // the reader does while playing - a controller speaking, a press the interface took - and a
        // controller held down keeps saying so, so the state is kept here rather than asked for again.
        void apply_floating(bool floating) {
            if (g_floating == floating) {
                return;
            }

            g_floating = floating;

            // Nothing to fade until there is a window to fade, and the window that comes next starts
            // out on top of the interface by itself.
            if (!g_shown) {
                return;
            }

            JNIEnv *env = current_env();

            if (!env) {
                return;
            }

            env->CallStaticVoidMethod(g_class, g_set_floating,
                                      floating ? JNI_TRUE : JNI_FALSE);

            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
        }

        // Tells the window to come or go, once per change rather than once per frame: the fade itself
        // is android's to run, and asking it again every tick would only restart it.
        void apply_faded(bool faded) {
            if (g_faded == faded) {
                return;
            }

            g_faded = faded;

            if (!g_shown) {
                return;
            }

            JNIEnv *env = current_env();

            if (!env) {
                return;
            }

            env->CallStaticVoidMethod(g_class, g_set_faded, faded ? JNI_TRUE : JNI_FALSE);

            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
        }
    }  // namespace

    bool NativeChat::prepare() {
        if (g_ready) {
            return true;
        }

        GLFMDisplay *display = Window::get().get_display();

        if (!display) {
            return false;
        }

        auto *activity = static_cast<ANativeActivity *>(glfmGetAndroidActivity(display));

        g_ready = resolve(activity);
        return g_ready;
    }

    void NativeChat::activate() {
        g_wanted = true;

        if (prepare()) {
            send_line("[Welcome] Welcome to MapleStory!!", YELLOW, NOTICE);
        }
    }

    void NativeChat::deactivate() {
        g_wanted = false;
        close();
    }

    void NativeChat::update() {
        for (auto iter : g_cooldowns) {
            iter.second -= Constants::TIMESTEP;
        }

        if (!g_wanted || !available()) {
            return;
        }

        // The window is not part of the picture the transition fades, so it waits for that to hand
        // the scene back: appearing over a loading screen, or over a map that is still being put
        // together, reads as a window that arrived before the game did.
        if (!g_shown) {
            if (Window::get().is_faded_in()) {
                open();
            }
        }
        else {
            // The window is not part of the picture the transition fades, so it is told to go out with
            // it and come back with it: left to itself it would sit at full strength over a black
            // loading screen, which is the one thing about it that never belonged to the scene.
            if (Window::get().is_fading_out()) {
                apply_faded(false);
            }
            else if (Window::get().is_fading_in() || Window::get().is_faded_in()) {
                apply_faded(true);
            }

            send_typed();
        }
    }

    bool NativeChat::open() {
        if (g_shown) {
            return true;
        }

        if (!prepare()) {
            return false;
        }

        GLFMDisplay *display = Window::get().get_display();
        int surface_width = 0;
        int surface_height = 0;
        glfmGetDisplaySize(display, &surface_width, &surface_height);

        if (surface_width <= 0 || surface_height <= 0) {
            return false;
        }

        // Interface units are what every window in the game is measured in, and the frame they reach
        // is the surface, so one unit is this many device pixels - the same factor the interface
        // projection uses. The native window has to be sized by it or its lettering would come out a
        // different height than the rest of the interface it sits next to.
        const float px_per_unit =
                static_cast<float>(surface_width)
                / static_cast<float>(Constants::Constants::get().get_ui_viewwidth());

        const int16_t view_height = Constants::Constants::get().get_ui_viewheight();
        const int16_t window_height = Constants::NATIVE_CHAT_CHROME
                                      + Constants::NATIVE_CHAT_ROWS * Constants::NATIVE_CHAT_ROW_HEIGHT;

        // Anchored low and left, where the chat bar's scrollback used to be.
        int32_t x = to_pixels(Constants::NATIVE_CHAT_MARGIN_X, px_per_unit);
        int32_t y = to_pixels(view_height - window_height - Constants::NATIVE_CHAT_MARGIN_Y,
                              px_per_unit);
        int32_t width = to_pixels(Constants::NATIVE_CHAT_WIDTH, px_per_unit);
        int32_t rows = to_pixels(Constants::NATIVE_CHAT_ROWS * Constants::NATIVE_CHAT_ROW_HEIGHT,
                                 px_per_unit);

        if (y < 0) {
            y = 0;
        }

        JNIEnv *env = current_env();

        if (!env) {
            return false;
        }

        auto *activity = static_cast<ANativeActivity *>(glfmGetAndroidActivity(display));

        // The window that comes into the world comes into it on top of the interface, and the window
        // says so for itself as it is built; this is kept in step with it so that the first press the
        // interface takes afterwards is taken as the change it is. The window also comes in at nothing
        // and is asked to fade up by itself, so the game's side of that starts at full strength.
        g_floating = true;
        g_faded = true;

        // The window is a window of its own, so Android composites it above the game's surface
        // rather than under it, and it is the game's own frame that says how far it may be dragged.
        env->CallStaticVoidMethod(g_class, g_show, activity->clazz, px_per_unit,
                                  x, y, width, rows, Constants::NATIVE_CHAT_MAX_LINES,
                                  surface_width, surface_height);

        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return false;
        }

        g_shown = true;
        return true;
    }

    void NativeChat::close() {
        if (!g_shown) {
            return;
        }

        g_shown = false;

        JNIEnv *env = current_env();

        if (!env) {
            return;
        }

        env->CallStaticVoidMethod(g_class, g_hide);

        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }

    bool NativeChat::available() {
        return g_ready && g_class && g_append;
    }

    void NativeChat::send_line(const std::string &line,
                               LineType type,
                               Category category) {
        post(category, colored(type), line);
    }

    void NativeChat::display_message(Messages::Type line, LineType type) {
        if (g_cooldowns[line] > 0) {
            return;
        }

        std::string message = Messages::messages[line];
        send_line(message, type, BATTLE);

        g_cooldowns[line] = MESSAGE_COOLDOWN_;
    }

    void NativeChat::sink() {
        apply_floating(false);
    }

    void NativeChat::raise() {
        // Not while a direction is still being held. A press that gets through to the game under a
        // window that is out of the way is one the reader makes with the free hand while the other
        // is still on the stick, and a window that comes up in the middle of a run takes the
        // keyboard away from the game and leaves the reader steering nothing.
        if (Gamepad::get().movement_held()) {
            return;
        }

        apply_floating(true);
    }

    bool NativeChat::covers(int16_t x, int16_t y) {
        if (!g_shown || !available() || !g_touched) {
            return false;
        }

        JNIEnv *env = current_env();

        if (!env) {
            return false;
        }

        auto hit = env->CallStaticBooleanMethod(g_class, g_touched, static_cast<jint>(x),
                                                static_cast<jint>(y));

        if (env->ExceptionCheck()) {
            env->ExceptionClear();

            return false;
        }

        return hit == JNI_TRUE;
    }

    void NativeChat::post(Category category, Color::Name color, const std::string &line) {
        if (!available() || line.empty()) {
            return;
        }

        JNIEnv *env = current_env();

        if (!env) {
            return;
        }

        jstring text = env->NewStringUTF(line.c_str());

        if (!text) {
            env->ExceptionClear();
            return;
        }

        env->CallStaticVoidMethod(g_class, g_append, static_cast<jint>(category),
                                  static_cast<jint>(packed(color)), text);
        env->DeleteLocalRef(text);

        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }
}  // namespace ms
