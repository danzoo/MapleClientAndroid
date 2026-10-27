//	This file is part of the continued Journey MMORPG client
//	Copyright (C) 2015-2024  Daniel Allendorf, Ryan Payton, Bizhou Xing
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
#include "AndroidText.h"

#include <jni.h>
#include <android/bitmap.h>
#include <android/log.h>
#include <android/native_activity.h>

#define LOG_TAG "NativeText"

namespace ms {
namespace {
JavaVM *g_vm = nullptr;
jclass g_class = nullptr;          // global ref to openMapleClient/NativeText
jmethodID g_rasterize = nullptr;   // static Bitmap rasterize(int, float, boolean, float[])
jmethodID g_measure = nullptr;     // static float measure(int, float, boolean)

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

// FindClass on a natively-attached thread resolves against the system loader and cannot see app
// classes, so reach the helper through the activity's own class loader instead.
jclass load_helper_class(JNIEnv *env, ANativeActivity *activity) {
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

    jstring name = env->NewStringUTF("openMapleClient.NativeText");
    auto helper = static_cast<jclass>(env->CallObjectMethod(class_loader, load_class, name));

    env->DeleteLocalRef(name);
    env->DeleteLocalRef(class_loader);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }

    return helper;
}
}  // namespace

bool AndroidText::init(GLFMDisplay *display) {
    if (g_class && g_rasterize && g_measure) {
        return true;
    }

    auto *activity = static_cast<ANativeActivity *>(glfmGetAndroidActivity(display));

    if (!activity || !activity->vm) {
        return false;
    }

    g_vm = activity->vm;

    JNIEnv *env = current_env();

    if (!env) {
        return false;
    }

    jclass local = load_helper_class(env, activity);

    if (!local) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "helper class not found");
        return false;
    }

    g_class = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);

    g_rasterize = env->GetStaticMethodID(g_class, "rasterize",
                                         "(IFZ[F)Landroid/graphics/Bitmap;");

    if (!g_rasterize) {
        env->ExceptionClear();
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "rasterize method not found");
        return false;
    }

    g_measure = env->GetStaticMethodID(g_class, "measure", "(IFZ)F");

    if (!g_measure) {
        env->ExceptionClear();
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "measure method not found");
        return false;
    }

    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "bridge ready");
    return true;
}

bool AndroidText::available() {
    return g_class != nullptr && g_rasterize != nullptr && g_measure != nullptr;
}

bool AndroidText::measure(uint32_t codepoint, float px_size, bool bold, float &advance) {
    if (!available()) {
        return false;
    }

    JNIEnv *env = current_env();

    if (!env) {
        return false;
    }

    jfloat result = env->CallStaticFloatMethod(g_class, g_measure,
                                               static_cast<jint>(codepoint),
                                               static_cast<jfloat>(px_size),
                                               static_cast<jboolean>(bold));

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }

    advance = result;
    return true;
}

bool AndroidText::rasterize(uint32_t codepoint, float px_size, bool bold, GlyphBitmap &out) {
    if (!available()) {
        return false;
    }

    JNIEnv *env = current_env();

    if (!env) {
        return false;
    }

    jfloatArray metrics = env->NewFloatArray(6);

    if (!metrics) {
        env->ExceptionClear();
        return false;
    }

    jobject bitmap = env->CallStaticObjectMethod(g_class, g_rasterize,
                                                 static_cast<jint>(codepoint),
                                                 static_cast<jfloat>(px_size),
                                                 static_cast<jboolean>(bold),
                                                 metrics);

    jfloat m[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    env->GetFloatArrayRegion(metrics, 0, 6, m);
    env->DeleteLocalRef(metrics);

    out.advance = m[0];
    out.bearing_x = m[1];
    out.bearing_y = m[2];
    out.ascent = m[5];

    // A null bitmap is not an error: it is a blank glyph (space and friends) whose advance still
    // matters for layout but has no ink to upload.
    if (!bitmap) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return false;
        }

        out.width = 0;
        out.height = 0;
        out.alpha.clear();
        return false;
    }

    AndroidBitmapInfo info;

    if (AndroidBitmap_getInfo(env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS) {
        env->DeleteLocalRef(bitmap);
        return false;
    }

    void *pixels = nullptr;

    if (AndroidBitmap_lockPixels(env, bitmap, &pixels) != ANDROID_BITMAP_RESULT_SUCCESS) {
        env->DeleteLocalRef(bitmap);
        return false;
    }

    out.width = static_cast<int32_t>(info.width);
    out.height = static_cast<int32_t>(info.height);

    const size_t count = static_cast<size_t>(info.width) * info.height;
    out.alpha.resize(count);

    // The glyph is drawn white, so its coverage lives entirely in the alpha channel; read that and
    // drop the colour. stride guards against the bitmap being padded per row.
    const uint8_t *base = static_cast<const uint8_t *>(pixels);

    for (uint32_t y = 0; y < info.height; ++y) {
        const uint32_t *row = reinterpret_cast<const uint32_t *>(base + y * info.stride);

        for (uint32_t x = 0; x < info.width; ++x) {
            out.alpha[static_cast<size_t>(y) * info.width + x] =
                    static_cast<uint8_t>((row[x] >> 24) & 0xFF);
        }
    }

    AndroidBitmap_unlockPixels(env, bitmap);
    env->DeleteLocalRef(bitmap);

    return true;
}
}  // namespace ms
