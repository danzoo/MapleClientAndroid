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
#include "SocketAsio.h"

#include <android/log.h>
#include <utility>

#ifdef USE_ASIO
namespace ms {
namespace {
// How much of the stream to ask for at a time. A read delivers whatever the other end has sent and
// the operating system has assembled, so this is only the ceiling on one callback, not a packet
// boundary - framing is the caller's job.
constexpr size_t READ_CHUNK = MAX_PACKET_LENGTH;
}  // namespace

SocketAsio::SocketAsio() :
    work_(ioservice_.get_executor()),
    resolver_(ioservice_),
    socket_(ioservice_),
    deadline_(ioservice_) {
    read_buffer_.resize(READ_CHUNK);
}

SocketAsio::~SocketAsio() {
    if (socket_.is_open()) {
        error_code error;
        socket_.close(error);
    }
}

void SocketAsio::set_handlers(HandshakeFunc on_handshake, BytesFunc on_bytes,
                              FailedFunc on_failed) {
    on_handshake_ = std::move(on_handshake);
    on_bytes_ = std::move(on_bytes);
    on_failed_ = std::move(on_failed);
}

bool SocketAsio::current(uint32_t seq) const {
    return seq == seq_;
}

void SocketAsio::fail(const char *step, const error_code &error) {
    // Retiring the sequence number here is what silences every other completion still queued for
    // this attempt, including the deadline this is called from and any read or write in flight.
    seq_++;
    writing_ = false;
    tx_.clear();

    error_code ignored;
    deadline_.cancel(ignored);
    socket_.close(ignored);

    __android_log_print(ANDROID_LOG_ERROR, "SocketAsio", "%s: %s", step,
                        error.message().c_str());

    if (on_failed_) {
        on_failed_(step);
    }
}

void SocketAsio::close() {
    // Withdrawing the attempt first: nothing a step of it reports afterwards may be taken for the
    // connection that comes next.
    seq_++;
    writing_ = false;
    tx_.clear();

    error_code error;
    deadline_.cancel(error);
    // A socket that never finished connecting has nothing to shut down, and that is not a reason to
    // refuse the close - the caller is about to dial again.
    socket_.shutdown(tcp::socket::shutdown_both, error);
    socket_.close(error);
}

void SocketAsio::start_connect(const char *address, const char *port, uint32_t timeout_ms) {
    // Copied, because the caller's two strings belong to a frame that has already moved on by the
    // time this runs.
    const std::string host = address != nullptr ? address : "";
    const std::string service = port != nullptr ? port : "";
    ioservice_.post([this, host, service, timeout_ms]() {
        connect_now(host, service, timeout_ms);
    });
}

void SocketAsio::connect_now(std::string address, std::string port, uint32_t timeout_ms) {
    const uint32_t seq = ++seq_;
    writing_ = false;
    tx_.clear();

    error_code error;
    socket_.close(error);

    // One deadline for the whole sequence: a name that does not resolve, a host that does not answer,
    // and a server that accepts the connection and then says nothing all end the same way.
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

            read_handshake(seq);
        });
    });
}

void SocketAsio::read_handshake(uint32_t seq) {
    // The key is a fixed number of bytes and can arrive in pieces, so it is read as a whole. A single
    // read_some of it used to have two ways to go wrong at once: a short delivery was called a failed
    // connect, and the rest of the key was left in the stream to be read as the length of the first
    // packet.
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

        // The callback builds this connection's cipher out of the key and may already want the
        // packets that were held for it, so the read loop only starts once it has come back.
        if (on_handshake_) {
            on_handshake_(handshake_);
        }

        if (current(seq)) {
            start_read(seq);
        }
    });
}

void SocketAsio::start_read(uint32_t seq) {
    socket_.async_read_some(asio::buffer(read_buffer_),
                            [this, seq](const error_code &error, size_t length) {
        if (!current(seq)) {
            return;
        }

        if (error) {
            // End of file, or the other end went away. There is nothing further to read and no point
            // reading on for a server that has stopped talking.
            fail("link dropped", error);
            return;
        }

        if (on_bytes_ && length > 0) {
            on_bytes_(read_buffer_.data(), length);
        }

        // The bytes may have been a framing error, which ends the connection from inside the
        // callback, so the attempt has to be checked again before asking for more.
        if (current(seq)) {
            start_read(seq);
        }
    });
}

void SocketAsio::write(std::vector<int8_t> frame) {
    ioservice_.post([this, frame = std::move(frame)]() mutable {
        if (!socket_.is_open()) {
            return;
        }

        tx_.push_back(std::move(frame));
        if (!writing_) {
            write_next(seq_);
        }
    });
}

void SocketAsio::write_next(uint32_t seq) {
    if (!current(seq) || tx_.empty()) {
        writing_ = false;
        return;
    }

    writing_ = true;
    // A whole frame in one call: the two dispatches this replaces could have put a header on the
    // wire with its body still to come, and a server reading as fast as it writes does not get to
    // see a packet in pieces.
    asio::async_write(socket_, asio::buffer(tx_.front()),
                      [this, seq](const error_code &error, size_t /*written*/) {
        if (!current(seq)) {
            writing_ = false;
            return;
        }

        if (error) {
            writing_ = false;
            // A send the socket refused means the other end is gone; there is no point reading on for
            // a server that has stopped taking packets.
            fail("send refused", error);
            return;
        }

        tx_.pop_front();
        write_next(seq);
    });
}

void SocketAsio::run() {
    ioservice_.run();
}

void SocketAsio::stop() {
    ioservice_.stop();
}
}  // namespace ms
#endif
