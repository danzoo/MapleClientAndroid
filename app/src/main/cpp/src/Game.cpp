//	This file is part of the continued Journey MMORPG client
//	Copyright (C) 2015-2020  Daniel Allendorf, Ryan Payton
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
#include "Game.h"
#include <chrono>
#include <iostream>

#ifdef __ANDROID__
#include <android/log.h>
#endif

#include "Configuration.h"
#include "Constants.h"
#include "Gameplay/Stage.h"
#include "IO/GamepadConfig.h"
#include "IO/UI.h"
#include "IO/Window.h"
#include "Net/PacketProcessor.h"
#include "Timer.h"
#include "Constants.h"
#include "Util/HardwareInfo.h"
#include "Util/ImgFiles.h"
#include "Singleton.h"

namespace ms {
Game::Game() {
    HardwareInfo();
}

Error Game::init(GLFMDisplay *pApp) {
    if (Error error = PacketProcessor::get().init(pApp)) {
        return error;
    }
    std::cout << "Session init success." << std::endl;
    if (Error error = ImgFiles::init(pApp)) {
        return error;
    }
    std::cout << "ImgFiles init success." << std::endl;

    // The settings file is kept in the directory ImgFiles was just given, so anything read out of it
    // before now was read from wherever the process happened to start. The file can be gone through
    // again once that directory is known.
    Configuration::get().load();

    // And written back straight away, so that the very first start leaves a file behind in that
    // directory instead of waiting for something to be changed. Every later start reads it again and
    // saves the same values, so nothing is lost by this; what it guarantees is that the settings -
    // among them the account id the login screen remembers - have somewhere to be by the time the
    // app is next killed rather than quit.
    Configuration::get().save();

    // A controller slot is dispatched as a key of its own, so the bindings read out of the file
    // above have to be installed in the keyboard's map before any controller event is handled.
    GamepadConfig::get();

    if (Error error = Window::get().init(pApp)) {
        return error;
    }
    std::cout << "Window init success." << std::endl;
    if (Error error = Music::init()) {
        return error;
    }
    std::cout << "Music init success." << std::endl;
    if (Error error = Sound::init()) {
        return error;
    }
    std::cout << "Sound init success." << std::endl;

    return Error::NONE;
}

void Game::update() {
    Window::get().update();
    Stage::get().update();
    UI::get().update();
    PacketProcessor::get().process();
    Music::update_context();
}

void Game::draw(float alpha) {
    Window::get().begin();
    Stage::get().draw(alpha);
    UI::get().draw(alpha);
    Window::get().end();
}

void Game::frame() {
    using clock = std::chrono::steady_clock;

    const int64_t timestep = static_cast<int64_t>(Constants::TICK_SECONDS * 1000000);

    // A frame that arrives late - a surface that was backgrounded, one long stall - reports the
    // whole gap at once. Running every update that gap bought would spend the next several frames
    // catching up before drawing anything, which is invisible work followed by another stall, so the
    // gap is cut short at four updates and the simulation lets real time pull ahead of it instead.
    const int64_t max_catchup = 4 * timestep;

    // This clock belongs to the loop alone. Timer::get() is shared with the map-load and cash-shop
    // timings, which restart it from wherever they are reached, and a restart in the middle of an
    // update would quietly hand the next frame a shorter interval than it was owed.
    static clock::time_point last = clock::now();
    static int64_t accumulator = timestep;

    const clock::time_point now = clock::now();
    int64_t elapsed = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now - last).count());
    last = now;

    if (elapsed > max_catchup) {
        elapsed = max_catchup;
    }

    // Spend the wall clock on whole updates, as many as it buys. The timestep is fixed, so the same
    // force always integrates to the same trajectory no matter how many updates a frame happened to
    // run into.
    uint32_t updates = 0;
    for (accumulator += elapsed; accumulator >= timestep; accumulator -= timestep) {
        update();
        updates++;
    }

    // The two rates this loop separates are only observable together: the panel rate is counted by
    // Window::count_frames(), and without this the number of updates a second actually buys would
    // just be arithmetic on a constant nobody checks.
    if (Configuration::get().get_show_fps()) {
        constexpr uint32_t LOG_WINDOW = 120;
        static clock::time_point log_start;
        static uint32_t logged_frames = 0;
        static uint32_t logged_updates = 0;

        if (logged_frames++ == 0) {
            // The interval this frame just spent is the one that ended at it, so the window starts
            // one interval before the first frame counted in it - otherwise the span is short by one
            // frame period and every rate read out of it comes out high by that much.
            log_start = now - std::chrono::microseconds(elapsed);
        }
        logged_updates += updates;

        if (logged_frames == LOG_WINDOW) {
            const double seconds = std::chrono::duration<double>(now - log_start).count();
            __android_log_print(ANDROID_LOG_INFO, "Game", "update %.1f /s in %.2f updates per frame",
                                logged_updates / seconds,
                                static_cast<double>(logged_updates) / logged_frames);
            logged_frames = 0;
            logged_updates = 0;
        }
    }

    // Whatever the last update did not pay for is how far into the next one this picture belongs,
    // which is what lets the frame rate be held to FRAME_RATE while the simulation runs at its own.
    draw(static_cast<float>(accumulator) / timestep);
}

bool Game::is_running() {
    return PacketProcessor::get().is_connected() && UI::get().not_quitted();
}

void Game::start(GLFMDisplay *pApp) {
    // Initialize and check for errors
    if (Error error = init(pApp)) {
        auto message = error.get_message();
        auto details = error.get_details();
        bool can_retry = error.can_retry();

        std::cout << "Error: " << message << std::endl;
        std::cout << "Message: " << details << std::endl;

        if (can_retry) {
            std::cout << "Enter 'retry' to try again." << std::endl;

            std::string command;
            std::cin >> command;

            if (command == "retry") {
                start(pApp);
            }
        }
    }
}
}  // namespace ms