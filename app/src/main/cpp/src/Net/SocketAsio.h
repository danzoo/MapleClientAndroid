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
#include <deque>
#include <functional>
#include <vector>

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

// Class that wraps an asio socket.
//
// Everything this class owns - the socket, the io_service, the deadline and both stream buffers -
// belongs to the single thread calling run(). Nothing here is safe to use before run() has been
// entered by that thread and nothing here needs a lock: work from other threads only ever arrives
// through post(), which hands it to that same thread.
class SocketAsio {
public:
    // All three are called on the thread running run(), from inside it, so they may touch the
    // io_service they were called from without posting back.

    // The handshake key, once, for the connection that is now up. Everything the read loop reports
    // afterwards is a body byte of that same connection.
    using HandshakeFunc = std::function<void(const int8_t *handshake)>;
    // A run of stream bytes of the connection that the handshake announced. Framing is not this
    // class's business, so a packet can arrive in pieces and pieces of two packets can arrive
    // together.
    using BytesFunc = std::function<void(const int8_t *bytes, size_t length)>;
    // The connection ended: the deadline ran out, one of the steps failed, the other end stopped
    // answering, or a write was refused. There is no attempt in flight afterwards.
    using FailedFunc = std::function<void(const char *why)>;

    SocketAsio();
    ~SocketAsio();

    void set_handlers(HandshakeFunc on_handshake, BytesFunc on_bytes, FailedFunc on_failed);

    // Hands the work to the io thread; returns at once. Calling any other method of this class from
    // a thread that is not running run() is not supported except through this.
    template <typename CompletionHandler>
    void post(CompletionHandler &&handler) {
        ioservice_.post(std::forward<CompletionHandler>(handler));
    }

    // Starts the attempt: resolving the name, then dialling every address it returns in turn, then
    // reading the handshake key. Safe from any thread - it posts itself. timeout_ms bounds the whole
    // sequence, not each step.
    void start_connect(const char *address, const char *port, uint32_t timeout_ms);

    // Queues one already-framed packet (header and body together) and, if nothing is being written,
    // starts writing it. Safe from any thread - it posts itself. Frames leave in the order they were
    // queued.
    void write(std::vector<int8_t> frame);

    // Runs the io service until stop() is called. Call this on the network thread and nowhere else.
    void run();
    // Withdraws whatever attempt is in flight and lets run() return. Safe from any thread.
    void stop();

    // Drops the connection and everything queued for it, leaving the socket idle. On the io thread.
    void close();

private:
    void connect_now(std::string address, std::string port, uint32_t timeout_ms);
    void read_handshake(uint32_t seq);
    void start_read(uint32_t seq);
    void write_next(uint32_t seq);
    // Every attempt numbers itself and every handler carries the number of the attempt that queued
    // it, so a completion that arrives after a close() or after a newer start_connect is recognised
    // and dropped instead of being applied to the connection that has replaced it.
    bool current(uint32_t seq) const;
    void fail(const char *step, const error_code &error);

    io_service ioservice_;
    // A poll() on a service whose outstanding work count has reached zero stops it for good, and a
    // stopped service never runs a handler again. Holding the count here is what keeps run() awake
    // across the gaps between packets instead of returning the moment the socket goes quiet.
    asio::executor_work_guard<io_service::executor_type> work_;
    tcp::resolver resolver_;
    tcp::socket socket_;
    asio::steady_timer deadline_;

    uint32_t seq_ = 0;
    // A write is in flight, so anything queued behind it waits for its completion rather than being
    // started alongside it; two concurrent writes to one socket would interleave their bytes.
    bool writing_ = false;
    std::vector<int8_t> read_buffer_;
    std::deque<std::vector<int8_t>> tx_;
    int8_t handshake_[HANDSHAKE_LEN];

    HandshakeFunc on_handshake_;
    BytesFunc on_bytes_;
    FailedFunc on_failed_;
};
}  // namespace ms
#endif
