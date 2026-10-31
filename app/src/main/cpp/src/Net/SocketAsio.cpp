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
#include <android/log.h>
#include <chrono>
#include "SocketAsio.h"

#ifdef USE_ASIO
namespace ms {
SocketAsio::SocketAsio() :
    work_(ioservice_.get_executor()),
    resolver_(ioservice_),
    socket_(ioservice_),
    deadline_(ioservice_) {}

SocketAsio::~SocketAsio() {
    if (socket_.is_open()) {
        error_code error;
        socket_.close(error);
    }
}

bool SocketAsio::current(uint32_t seq) const {
    return seq == seq_;
}

void SocketAsio::fail(const char *step, const error_code &error) {
    // Retiring the sequence number here is what silences every other completion still queued for
    // this attempt, including the deadline this is called from.
    seq_++;
    connect_ = Connect::Failed;

    error_code ignored;
    deadline_.cancel(ignored);
    socket_.close(ignored);

    __android_log_print(ANDROID_LOG_ERROR, "SocketAsio", "%s: %s", step,
                        error.message().c_str());
}

void SocketAsio::start_connect(const char *address, const char *port, uint32_t timeout_ms) {
    const uint32_t seq = ++seq_;
    connect_ = Connect::Pending;

    error_code error;
    socket_.close(error);

    // One deadline for the whole sequence: a name that does not resolve, a host that does not
    // answer, and a server that accepts the connection and then says nothing all end the same way.
    deadline_.expires_after(std::chrono::milliseconds(timeout_ms));
    deadline_.async_wait([this, seq](const error_code &error) {
        // Cancelling the timer is how a finished attempt withdraws its deadline, so a wait that came
        // back aborted is a connect that got there first rather than a timeout.
        if (error || !current(seq)) {
            return;
        }

        fail("deadline", asio::error::timed_out);
    });

    resolver_.async_resolve(address, port,
                            [this, seq](const error_code &error,
                                        tcp::resolver::results_type results) {
        if (!current(seq)) {
            return;
        }

        if (error) {
            fail("resolve", error);
            return;
        }

        // Every address the name returns, tried in turn: asio::async_connect over the whole range is
        // the non-blocking form of the loop the blocking connect ran internally over the iterator.
        asio::async_connect(socket_, results,
                            [this, seq](const error_code &error, const tcp::endpoint &) {
            if (!current(seq)) {
                return;
            }

            if (error) {
                fail("connect", error);
                return;
            }

            // The key is a fixed number of bytes and can arrive in pieces, so it is read as a whole.
            // A single read_some of it used to have two ways to go wrong at once: a short delivery
            // was called a failed connect, and the rest of the key was left in the stream to be read
            // as the length of the first packet.
            asio::async_read(socket_, asio::buffer(handshake_),
                             [this, seq](const error_code &error, size_t read) {
                if (!current(seq)) {
                    return;
                }

                if (error || read != HANDSHAKE_LEN) {
                    fail("handshake", error);
                    return;
                }

                error_code ignored;
                deadline_.cancel(ignored);
                connect_ = Connect::Ready;
            });
        });
    });
}

void SocketAsio::poll() {
    // Nothing here runs on its own clock: a completion is only reached when a frame asks, which is
    // what keeps the whole machine on the render thread with no thread of its own and no locks. The
    // call takes whatever is ready and returns; it never waits.
    ioservice_.poll();
}

Connect SocketAsio::connect_state() const {
    return connect_;
}

const int8_t *SocketAsio::handshake() const {
    return handshake_;
}

void SocketAsio::close() {
    // Withdrawing the attempt first: nothing a step of it reports afterwards may be taken for the
    // connection that comes next.
    seq_++;
    connect_ = Connect::Idle;

    error_code error;
    deadline_.cancel(error);
    // A socket that never finished connecting has nothing to shut down, and that is not a reason to
    // refuse the close - the caller is about to dial again.
    socket_.shutdown(tcp::socket::shutdown_both, error);
    socket_.close(error);
}

size_t SocketAsio::receive(bool *recvok) {
    if (!socket_.is_open()) {
        return 0;
    }

    if (socket_.available() > 0) {
        error_code error;
        size_t result = socket_.read_some(asio::buffer(buffer_), error);
        *recvok = !error;

        return result;
    }

    return 0;
}

const int8_t *SocketAsio::get_buffer() const {
    return buffer_;
}

bool SocketAsio::dispatch(const int8_t *bytes, size_t length) {
    error_code error;
    size_t result = asio::write(socket_, asio::buffer(bytes, length), error);

    return !error && (result == length);
}
}  // namespace ms
#endif