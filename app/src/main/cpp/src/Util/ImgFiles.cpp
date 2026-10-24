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
#include <imgwz/nx.hpp>

#include <sys/stat.h>

#include <stdexcept>
#include <string>

#ifdef __ANDROID__
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
Error init(GLFMDisplay *pApp) {
    const std::string root = game_path(pApp);
    const std::string data = root + "/Data";
    try {
        nl::nx::load_all(is_directory(data) ? data : root);
        if (nl::nx::ui.size() == 0) {
            throw std::runtime_error("No game files in " + data);
        }
    } catch (const std::exception &ex) {
        static const std::string message = ex.what();
        return Error(Error::Code::IMGWZ, message.c_str());
    }

    constexpr const char *POSTCHAOS_BITMAP =
        "Login.img/WorldSelect/BtChannel/layer:bg";

    if (nl::nx::ui.resolve(POSTCHAOS_BITMAP).data_type()
        != nl::node::type::bitmap) {
        return Error::Code::WRONG_UI_FILE;
    }

    return Error::Code::NONE;
}
}  // namespace ms::ImgFiles
