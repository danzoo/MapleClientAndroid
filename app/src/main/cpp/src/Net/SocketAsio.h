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

#include "../MSClient.h"

#ifdef USE_ASIO
#include "NetConstants.h"

#define BOOST_DATE_TIME_NO_LIB
#define BOOST_REGEX_NO_LIB
#include "asio.hpp"

namespace ms {
#ifndef USE_CRYPTO
const size_t HANDSHAKE_LEN = 2;
#else
const size_t HANDSHAKE_LEN = 16;
#endif

using asio::error_code;
using asio::io_service;
using asio::ip::tcp;

// How far a connect that is being carried out across frames has got.
enum class Connect {
    // Nothing in flight and no connection.
    Idle,
    // Resolving, dialling, or waiting for the handshake key. The three run in sequence and each is
    // only advanced by poll(), which is what keeps an attempt from ever occupying the render thread.
    Pending,
    // The key is in handshake() and the socket can be read from and written to.
    Ready,
    // The attempt ended without a connection, either because one of the three steps failed or
    // because the deadline ran out.
    Failed
};

// Class that wraps an asio socket.
class SocketAsio {
public:
    SocketAsio();
    ~SocketAsio();

    // Starts the connect: resolving the name, then dialling every address it returns in turn, then
    // reading the handshake key. None of that happens before this returns - a connection attempt
    // takes as long as the network and the other machine take, and the frame loop cannot be held
    // waiting for them - so poll() has to be called once a frame until connect_state() is no longer
    // Connect::Pending. timeout_ms bounds the whole sequence, not each step.
    void start_connect(const char *address, const char *port, uint32_t timeout_ms);

    // Runs whatever asio has finished since the last call, which is also how a connect advances.
    void poll();

    Connect connect_state() const;

    // The handshake key, valid once connect_state() is Connect::Ready. Kept apart from the packet
    // buffer so that a key arriving in pieces cannot be mistaken for the start of a packet.
    const int8_t *handshake() const;

    // Withdraws whatever attempt is in flight and closes the socket.
    void close();
    size_t receive(bool *connected);
    const int8_t *get_buffer() const;
    bool dispatch(const int8_t *bytes, size_t length);

private:
    // Every attempt numbers itself and every handler carries the number of the attempt that queued
    // it, so a completion that arrives after a close() or after a newer start_connect is recognised
    // and dropped instead of being applied to the connection that has replaced it.
    bool current(uint32_t seq) const;
    void fail(const char *step, const error_code &error);

    io_service ioservice_;
    // A poll() on a service whose outstanding work count has reached zero stops it for good, and a
    // stopped service never runs a handler again. An in-flight connect is not counted as work, so
    // the count is back at zero by the time the channel hop asks for the second poll(); holding the
    // count here is what keeps that attempt, and its deadline, alive.
    asio::executor_work_guard<io_service::executor_type> work_;
    tcp::resolver resolver_;
    tcp::socket socket_;
    asio::steady_timer deadline_;

    uint32_t seq_ = 0;
    Connect connect_ = Connect::Idle;
    int8_t buffer_[MAX_PACKET_LENGTH];
    int8_t handshake_[HANDSHAKE_LEN];
};
}  // namespace ms
#endif