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
#include "NativeNpcTalk.h"

#include <jni.h>

#include <cmath>

#include "../../Constants.h"
#include "../Graphics/Text.h"
#include "../Graphics/Texture.h"
#include "Window.h"
#include "glfm.h"

namespace ms {
    namespace {
        JavaVM *g_vm = nullptr;
        jclass g_class = nullptr;
        jmethodID g_open = nullptr;
        jmethodID g_close = nullptr;
        jmethodID g_display = nullptr;
        bool g_ready = false;
        bool g_shown = false;

        // The art last handed over, kept so that a dialog that only changed its words does not carry
        // the same pictures across again. The typewriter rebuilds the list for every character it
        // lets out, and the character speaking is a canvas of some tens of thousands of pixels: what
        // has to travel for a letter is the letter, not the listener.
        //
        // The window is given nothing for these when the list turns out to be the same, and it keeps
        // what it was last given - which is sound because a picture record names its art by a slot in
        // one of the two, so both travel or neither does.
        std::vector<std::string> g_names;
        std::vector<int32_t> g_pixels;
        bool g_art_valid = false;

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
        // reached through the activity's own loader.
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

            jstring name = env->NewStringUTF("openMapleClient.NativeNpcTalk");
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
            if (g_class && g_open && g_close && g_display) {
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

            g_open = env->GetStaticMethodID(g_class, "open", "(Landroid/app/Activity;FII)V");
            g_close = env->GetStaticMethodID(g_class, "close", "()V");
            g_display = env->GetStaticMethodID(g_class, "display",
                                               "([I[Ljava/lang/String;[Ljava/lang/String;[I)V");

            if (env->ExceptionCheck() || !g_open || !g_close || !g_display) {
                env->ExceptionClear();
                env->DeleteGlobalRef(g_class);
                g_class = nullptr;
                g_open = nullptr;
                g_close = nullptr;
                g_display = nullptr;

                return false;
            }

            return true;
        }

        int32_t packed(Color::Name name) {
            const GLfloat *rgb = Color::colors[name];

            auto red = static_cast<int32_t>(rgb[0] * 255.0f + 0.5f);
            auto green = static_cast<int32_t>(rgb[1] * 255.0f + 0.5f);
            auto blue = static_cast<int32_t>(rgb[2] * 255.0f + 0.5f);

            return (0xFF << 24) | (red << 16) | (green << 8) | blue;
        }

        // The name of an exported picture, from the path it has inside the archive. The export turns
        // every separator into an underscore; this has to agree with it exactly, or the window draws
        // nothing at all for that piece and the reader sees a dialog with holes in it.
        std::string asset_name(const std::string &path) {
            std::string name = path;

            for (char &c : name) {
                if (c == '/') {
                    c = '_';
                }
            }

            return name;
        }

        jintArray new_int_array(JNIEnv *env, const std::vector<int32_t> &values) {
            jintArray result = env->NewIntArray(static_cast<jsize>(values.size()));

            if (!result) {
                return nullptr;
            }

            if (!values.empty()) {
                env->SetIntArrayRegion(result, 0, static_cast<jsize>(values.size()),
                                       values.data());
            }

            return result;
        }

        jobjectArray new_string_array(JNIEnv *env, const std::vector<std::string> &values) {
            jclass string_class = env->FindClass("java/lang/String");

            if (!string_class) {
                env->ExceptionClear();
                return nullptr;
            }

            jobjectArray result =
                    env->NewObjectArray(static_cast<jsize>(values.size()), string_class, nullptr);

            env->DeleteLocalRef(string_class);

            if (!result) {
                return nullptr;
            }

            std::vector<jchar> chars;

            for (size_t i = 0; i < values.size(); i++) {
                const std::string &text = values[i];

                chars.clear();

                size_t pos = 0;

                while (pos < text.size()) {
                    // Only ever fed text the client itself decoded out of an archive, so a byte
                    // above 0x7F means the run was not convertible and is dropped rather than
                    // guessed at.
                    const auto byte = static_cast<unsigned char>(text[pos++]);

                    if (byte < 0x80) {
                        chars.push_back(static_cast<jchar>(byte));
                        continue;
                    }

                    uint32_t codepoint = byte & 0x3F;
                    uint32_t extra = (byte >= 0xF0) ? 3 : (byte >= 0xE0) ? 2 : 1;

                    if (extra == 0 || pos + extra > text.size()) {
                        continue;
                    }

                    codepoint = (byte & ((1 << (6 - extra)) - 1)) << (6 * extra);

                    for (uint32_t k = 0; k < extra; k++) {
                        const auto next = static_cast<unsigned char>(text[pos++]);

                        if ((next & 0xC0) != 0x80) {
                            codepoint = 0xFFFFFFFF;
                            break;
                        }

                        codepoint |= static_cast<uint32_t>(next & 0x3F) << (6 * (extra - 1 - k));
                    }

                    if (codepoint > 0xFFFF) {
                        if (codepoint > 0x10FFFF) {
                            continue;
                        }

                        codepoint -= 0x10000;
                        chars.push_back(static_cast<jchar>(0xD800 + (codepoint >> 10)));
                        chars.push_back(static_cast<jchar>(0xDC00 + (codepoint & 0x3FF)));
                    }
                    else {
                        chars.push_back(static_cast<jchar>(codepoint));
                    }
                }

                jstring value = env->NewString(chars.data(), static_cast<jsize>(chars.size()));

                if (!value) {
                    env->ExceptionClear();
                    break;
                }

                env->SetObjectArrayElement(result, static_cast<jsize>(i), value);
                env->DeleteLocalRef(value);
            }

            return result;
        }
    }  // namespace

    void NativeNpcTalk::List::push(int32_t kind,
                                   int32_t a,
                                   int32_t b,
                                   int32_t c,
                                   int32_t x1,
                                   int32_t y1,
                                   int32_t x2,
                                   int32_t y2) {
        ops_.insert(ops_.end(), {kind, a, b, c, x1, y1, x2, y2});
    }

    bool NativeNpcTalk::List::empty() const {
        return ops_.empty();
    }

    void NativeNpcTalk::List::picture(const Texture &source,
                                      const DrawArgument &where,
                                      const std::string &asset) {
        if (!source.is_valid()) {
            return;
        }

        const Rectangle<int16_t> box =
                where.get_rectangle(source.get_origin(), source.get_dimensions());

        names_.push_back(asset_name(asset));

        push(PICTURE, static_cast<int32_t>(names_.size()) - 1, 0, 0,
             box.left(), box.top(), box.right(), box.bottom());
    }

    void NativeNpcTalk::List::icon(const Texture &source, const DrawArgument &where) {
        const img::bitmap &bitmap = source.bitmap();

        if (!bitmap) {
            return;
        }

        const bool flipped = where.get_xscale() < 0.0f;

        Rectangle<int16_t> box =
                where.get_rectangle(source.get_origin(), source.get_dimensions());

        // A mirrored draw comes back with its two vertical edges exchanged, because that is how the
        // pass put the quad. The window wants a box, so the edges are put the right way round and the
        // mirroring is said out loud instead.
        if (flipped) {
            const int16_t left = box.left();

            box = Rectangle<int16_t>(box.right(), left, box.top(), box.bottom());
        }

        const int32_t width = bitmap.width();
        const int32_t height = bitmap.height();

        auto held = icons_.find(bitmap.id());

        if (held == icons_.end()) {
            std::vector<uint8_t> raw(bitmap.length());

            if (bitmap.decode_into(raw.data(), raw.size()) == 0) {
                return;
            }

            const int32_t offset = static_cast<int32_t>(pixels_.size());

            // The canvas is BGRA, the window is ARGB. Straight alpha, not the premultiplied form the
            // atlas keeps, because the platform is the one doing the blending now.
            for (int32_t i = 0; i < width * height; i++) {
                pixels_.push_back((static_cast<int32_t>(raw[4 * i + 3]) << 24)
                                  | (raw[4 * i + 2] << 16) | (raw[4 * i + 1] << 8) | raw[4 * i]);
            }

            held = icons_.emplace(bitmap.id(), offset).first;
        }

        push(flipped ? ICON_FLIPPED : ICON, held->second, width, height,
             box.left(), box.top(), box.right(), box.bottom());
    }

    void NativeNpcTalk::List::text(const Text &content,
                                   Point<int16_t> origin,
                                   Range<int16_t> clip) {
        if (content.empty()) {
            return;
        }

        float px;
        bool bold;
        Text::metrics(content.font(), px, bold);

        const std::vector<uint32_t> &codepoints = content.codepoints();
        const bool clipped = !clip.empty();

        for (const Text::Layout::Line &line : content.layout()) {
            for (const Text::Layout::Word &word : line.words) {
                std::string run;

                for (size_t pos = word.first; pos < word.last; pos++) {
                    const uint32_t codepoint = codepoints[pos];

                    // The same conversion the atlas goes through, only written out here rather than
                    // fed to a rasterizer.
                    if (codepoint < 0x80) {
                        run.push_back(static_cast<char>(codepoint));
                    }
                    else if (codepoint < 0x800) {
                        run.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
                        run.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                    }
                    else if (codepoint < 0x10000) {
                        run.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
                        run.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                        run.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                    }
                    else {
                        run.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
                        run.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
                        run.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                        run.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                    }
                }

                if (run.empty()) {
                    continue;
                }

                Color::Name color = word.color;

                if (color >= Color::Name::NUM_COLORS) {
                    color = content.color();
                }

                texts_.push_back(std::move(run));

                push(clipped ? TEXT_CLIPPED : TEXT,
                     static_cast<int32_t>(texts_.size()) - 1,
                     clipped ? clip.first() : 0,
                     clipped ? clip.second() : 0,
                     origin.x() + line.position.x() + content.layout().advance(word.first),
                     origin.y() + line.position.y(),
                     packed(color),
                     (static_cast<int32_t>(std::lround(px)) << 1) | (bold ? 1 : 0));
            }
        }
    }

    bool NativeNpcTalk::prepare() {
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

    bool NativeNpcTalk::available() {
        return prepare() && g_class && g_display;
    }

    bool NativeNpcTalk::open() {
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
        // projection uses. A dialog that did not use it would come out a different height than the
        // windows standing behind it.
        const float px_per_unit =
                static_cast<float>(surface_width)
                / static_cast<float>(Constants::Constants::get().get_ui_viewwidth());

        JNIEnv *env = current_env();

        if (!env) {
            return false;
        }

        auto *activity = static_cast<ANativeActivity *>(glfmGetAndroidActivity(display));

        env->CallStaticVoidMethod(g_class, g_open, activity->clazz, px_per_unit,
                                  surface_width, surface_height);

        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return false;
        }

        g_shown = true;
        // Whether the window put up is a new one or the one that was already there is decided over
        // there, so nothing about the art it holds can be assumed.
        g_art_valid = false;
        return true;
    }

    void NativeNpcTalk::close() {
        if (!g_shown) {
            return;
        }

        g_shown = false;
        g_art_valid = false;

        JNIEnv *env = current_env();

        if (!env) {
            return;
        }

        env->CallStaticVoidMethod(g_class, g_close);

        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }

    void NativeNpcTalk::show(const List &list) {
        if (!g_shown && !open()) {
            return;
        }

        JNIEnv *env = current_env();

        if (!env) {
            return;
        }

        const bool same_art = g_art_valid && list.names_ == g_names
                              && list.pixels_ == g_pixels;

        jintArray ops = new_int_array(env, list.ops_);
        jintArray pixels = same_art ? nullptr : new_int_array(env, list.pixels_);
        jobjectArray texts = new_string_array(env, list.texts_);
        jobjectArray names = same_art ? nullptr : new_string_array(env, list.names_);

        if (!ops || !texts || (!same_art && (!pixels || !names))) {
            env->ExceptionClear();

            if (ops) {
                env->DeleteLocalRef(ops);
            }

            if (pixels) {
                env->DeleteLocalRef(pixels);
            }

            if (texts) {
                env->DeleteLocalRef(texts);
            }

            if (names) {
                env->DeleteLocalRef(names);
            }

            return;
        }

        env->CallStaticVoidMethod(g_class, g_display, ops, texts, names, pixels);

        env->DeleteLocalRef(ops);
        env->DeleteLocalRef(pixels);
        env->DeleteLocalRef(texts);
        env->DeleteLocalRef(names);

        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            g_art_valid = false;

            return;
        }

        if (!same_art) {
            g_names = list.names_;
            g_pixels = list.pixels_;
        }

        g_art_valid = true;
    }

    void NativeNpcTalk::dismiss() {
        close();
    }
}  // namespace ms
