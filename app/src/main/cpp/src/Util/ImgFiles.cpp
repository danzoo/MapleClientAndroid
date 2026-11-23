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
#include "ImgFiles.h"

#include <imgwz/node.hpp>
#include <imgwz/wz.hpp>
#include <img_zip.hpp>

#include "../Gameplay/Physics/PhysicsParams.h"

#include <sys/stat.h>

#include <stdexcept>
#include <string>

#ifdef __ANDROID__
#include <android/log.h>
#include <android/native_activity.h>
#endif

namespace {
std::string game_path(GLFMDisplay *pApp) {
#ifdef __ANDROID__
    auto *activity =
        reinterpret_cast<ANativeActivity *>(glfmGetAndroidActivity(pApp));
    return activity->externalDataPath;
#else
    return ".";
#endif
}

bool is_directory(const std::string &path) {
    struct stat info;
    return ::stat(path.c_str(), &info) == 0 && (info.st_mode & S_IFDIR) != 0;
}
}  // namespace

namespace ms::ImgFiles {
namespace {
std::string app_root;
}

const std::string &root_path() {
    return app_root;
}

void set_root(GLFMDisplay *pApp) {
    app_root = game_path(pApp);
}

Error init(GLFMDisplay *pApp) {
    const std::string root = game_path(pApp);
    app_root = root;
    const std::string data = root + "/Data";
    const bool loose_data = is_directory(data);
    std::string data_root = loose_data ? data : root;
    try {
#ifdef IMG_FROM_ZIP
        // Select the backend before imgwz caches any directories or files. Debug uses
        // an existing Data directory directly; a Data file must be a valid STORE-only ZIP.
        // Release must not fall back to a loose tree if its archive is absent or invalid.
#ifdef IMG_ALLOW_LOOSE_DATA
        if (!loose_data) {
#else
        if (loose_data) {
            throw std::runtime_error("Release requires packaged Data, not a directory: " + data);
        }
        {
#endif
            if (!imgzip::open(data, data)) {
                throw std::runtime_error("Cannot open packaged Data (STORE-only ZIP required): " + data);
            }
            data_root = data;
#ifdef __ANDROID__
            __android_log_print(ANDROID_LOG_INFO, "imgzip", "reading packaged resources from %s",
                                data.c_str());
#endif
        }
#endif
        img::wz::load_all(data_root);
        if (img::wz::ui.size() == 0) {
            throw std::runtime_error("No game files in " + data);
        }
    } catch (const std::exception &ex) {
        static const std::string message = ex.what();
        return Error(Error::Code::IMGWZ, message.c_str());
    }

    constexpr const char *POSTCHAOS_BITMAP =
        "Login.img/WorldSelect/BtChannel/layer:bg";

    if (img::wz::ui.resolve(POSTCHAOS_BITMAP).data_type()
        != img::node::type::bitmap) {
        return Error::Code::WRONG_UI_FILE;
    }

    PhysicsParams::load();

    return Error::Code::NONE;
}
}  // namespace ms::ImgFiles
