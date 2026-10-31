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

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "../Error.h"
#include "../MSClient.h"
#include "Cryptography.h"
#include "Forwarder.h"

#ifdef USE_ASIO
#include "SocketAsio.h"
#include "glfm.h"

#else
#include "SocketWinsock.h"
#endif

namespace ms {
class Session {
public:
    // Where the connection stands. A connection is opened across frames, so none of these except
    // Connected means there is a socket to send on.
    enum class Link {
        // No attempt in flight: nothing has asked for a connection yet, or one was closed.
        Idle,
        // Opening. Packets written now are held for it.
        Connecting,
        // Up, keys read, packets go straight out.
        Connected,
        // The attempt gave up, or a connection that was up died.
        Failed
    };

    Session(std::unique_ptr<Forwarder> packet_forwarder);
    ~Session();

    // Start connecting using the address in hostip.txt and return at once: the attempt runs across
    // the following frames, so this says nothing about whether it succeeds.
    Error init(GLFMDisplay *pApp);
    // Send a packet to the server, or hold it if the connection is still being made
    void write(int8_t *bytes, size_t length);
    // Advance a connection in progress, then check for incoming packets and handle them
    void read();
    // Closes the current connection and opens a new one with default connection
    // settings
    void reconnect();
    // Closes the current connection and opens a new one
    void reconnect(const char *address, const char *port);
    // The attempt in progress has given up. The login wait dialog holds on for this instead of for
    // its own timeout, which is much longer.
    bool connect_failed() const;
    // A connection that was up and then died, as distinct from one that never came up: the game can
    // sit out the second and has nothing left to draw through the first.
    bool link_lost() const;

    std::string readHostIP(GLFMDisplay* pApp);

private:
    void connect(const char *host, const char *port);
    // Called off an attempt that is not coming back, so that every path that ends a connection says
    // so once and in the same way.
    void give_up(const char *why);
    void send(int8_t *bytes, size_t length);
    void flush_outbox();
    // The length of the packet that starts here, or 0 if there is nothing to read yet. Consumes the
    // header, including a header that came in pieces.
    bool read_length(const int8_t *&bytes, size_t &available);
    void process(const int8_t *bytes, size_t available);

    Cryptography cryptography_;
    std::unique_ptr<Forwarder> packet_forwarder_;

    int8_t buffer_[MAX_PACKET_LENGTH];
    size_t length_;
    size_t pos_;
    // A packet whose four length bytes have not all arrived yet, held until they do.
    int8_t header_[HEADER_LENGTH];
    size_t header_pos_;
    Link link_;
    // Set the first time a connection comes up and cleared by every deliberate new attempt, so that
    // losing a connection is distinguishable from still making one.
    bool had_connection_;

    // The address hostip.txt gave at startup, and the address this session was last pointed at. A
    // bare reconnect() goes back to the first: after a hop to a channel server, that is still the
    // server the login flow belongs to.
    std::string default_host_;
    std::string default_port_;
    std::string host_;
    std::string port_;

    // Packets written while a connection is being made, in the order they were written. Held
    // unencrypted because the cipher belongs to a connection and its key is only known once that
    // connection's handshake has been read.
    std::deque<std::vector<int8_t>> outbox_;

#ifdef USE_ASIO
    SocketAsio socket_;
#else
    SocketWinsock socket_;
#endif
};
}  // namespace ms